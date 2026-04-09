#include "yahboom_human_face_recognition.hpp"
#include "my_usart.h"
#include "my_usart1_user.h"
#include "app_mywifi.h"
#include "esp_code_scanner.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <time.h>

#include "dl_image.hpp"
#include "fb_gfx.h"

#include "human_face_detect_msr01.hpp"
#include "human_face_detect_mnp01.hpp"
#include "face_recognition_tool.hpp"
#include "my_user_iic.h"


#if CONFIG_MFN_V1
#if CONFIG_S8
#include "face_recognition_112_v1_s8.hpp"
#elif CONFIG_S16
#include "face_recognition_112_v1_s16.hpp"
#endif
#endif

#include "yahboom_ai_utils.hpp"

using namespace std;
using namespace dl;

static const char *TAG = "human_face_recognition";

static QueueHandle_t xQueueFrameI = NULL;
static QueueHandle_t xQueueEvent = NULL;
static QueueHandle_t xQueueFrameO = NULL;
static QueueHandle_t xQueueResult = NULL;

// Start from IDLE; STM32 menu explicitly selects FACE/QR/ENROLL/DELETE modes.
static recognizer_state_t gEvent = IDLE;
static bool gReturnFB = true;
static face_info_t recognize_result;

SemaphoreHandle_t xMutex;

typedef enum
{
    SHOW_STATE_IDLE,
    SHOW_STATE_DELETE,
    SHOW_STATE_RECOGNIZE,
    SHOW_STATE_ENROLL,
} show_state_t;

#define RGB565_MASK_RED 0xF800
#define RGB565_MASK_GREEN 0x07E0
#define RGB565_MASK_BLUE 0x001F
#define FRAME_DELAY_NUM 16
#define FACE_UNLOCK_HOLD_MS 5000
#define FACE_LOST_LOCK_COUNT 6
#define FACE_REARM_NOFACE_COUNT 6
#define FACE_REARM_HOLD_MS 3000
#define FACE_ENROLL_COOLDOWN_MS 3000
#define FACE_ENROLL_DUP_SIM_THRESHOLD 0.50f
#define QR_REPORT_DEDUP_MS 1500
#define QR_UNLOCK_HOLD_MS 3000
#define QR_UNLOCK_ACK_WAIT_MS 500
#define QR_UNLOCK_RETRY_COUNT 2
#define QR_UNLOCK_PAYLOAD "123456789"
#define QR_DEFAULT_LOCK_ID "LOCK001"
#define QR_LOCAL_USED_CACHE_SIZE 8
#ifndef QR_ALLOW_OFFLINE_WINDOW_FALLBACK
#define QR_ALLOW_OFFLINE_WINDOW_FALLBACK 0
#endif

static bool g_face_unlock_active = false;
static int64_t g_face_unlock_deadline_us = 0;
static uint8_t g_face_lost_count = 0;
static uint16_t g_no_face_log_div = 0;
static uint16_t g_no_frame_log_div = 0;
static uint16_t g_det_log_div = 0;
static int64_t g_last_enroll_us = 0;
static bool g_face_wait_rearm = false;
static uint8_t g_face_rearm_noface_count = 0;
static int64_t g_face_rearm_deadline_us = 0;
static int64_t g_last_qr_report_us = 0;
static char g_last_qr_payload[128] = {0};
static bool g_qr_unlock_active = false;
static int64_t g_qr_unlock_deadline_us = 0;
static char g_qr_used_tokens[QR_LOCAL_USED_CACHE_SIZE][128] = {0};
static uint8_t g_qr_used_token_count = 0;
static uint8_t g_qr_used_token_next = 0;

static bool qr_token_is_used(const char *token)
{
    if ((token == NULL) || (token[0] == '\0'))
    {
        return false;
    }

    for (uint8_t i = 0; i < g_qr_used_token_count; ++i)
    {
        if (strncmp(g_qr_used_tokens[i], token, sizeof(g_qr_used_tokens[i]) - 1U) == 0)
        {
            return true;
        }
    }

    return false;
}

static void qr_token_mark_used(const char *token)
{
    if ((token == NULL) || (token[0] == '\0'))
    {
        return;
    }

    if (qr_token_is_used(token))
    {
        return;
    }

    snprintf(g_qr_used_tokens[g_qr_used_token_next],
             sizeof(g_qr_used_tokens[g_qr_used_token_next]),
             "%s",
             token);

    if (g_qr_used_token_count < QR_LOCAL_USED_CACHE_SIZE)
    {
        g_qr_used_token_count++;
    }

    g_qr_used_token_next = (uint8_t)((g_qr_used_token_next + 1U) % QR_LOCAL_USED_CACHE_SIZE);
}

static uint8_t *g_qr_gray_buf = NULL;
static size_t g_qr_gray_buf_size = 0;
static int64_t g_qr_last_scan_us = 0;
static uint16_t g_qr_no_result_log_div = 0;
static uint16_t g_qr_format_err_log_div = 0;
static uint16_t g_qr_no_frame_log_div = 0;
static bool g_camera_in_qr_mode = false;
#define QR_SCAN_MIN_INTERVAL_US (500000)
#define QR_SCAN_MAX_W (240U)
#define QR_SCAN_MAX_H (176U)

static void camera_switch_mode(bool qr_mode)
{
    if (g_camera_in_qr_mode == qr_mode)
    {
        return;
    }

    // Some camera modules are unstable when switching sensor format at runtime.
    // Keep camera mode fixed and only switch recognition pipeline behavior.
    g_camera_in_qr_mode = qr_mode;
    if (qr_mode)
    {
        Uart_Send_Data((uint8_t *)"[CAM] mode QR keep rgb\r\n", 24);
    }
    else
    {
        Uart_Send_Data((uint8_t *)"[CAM] mode FACE keep rgb\r\n", 26);
    }
}

static bool qr_prepare_gray_buffer(uint32_t width, uint32_t height)
{
    size_t need = (size_t)width * (size_t)height;
    // Reuse existing buffer if it is already large enough.
    if ((g_qr_gray_buf != NULL) && (g_qr_gray_buf_size >= need))
    {
        return true;
    }

    if (g_qr_gray_buf != NULL)
    {
        heap_caps_free(g_qr_gray_buf);
        g_qr_gray_buf = NULL;
        g_qr_gray_buf_size = 0;
    }

    // Try internal RAM first, then SPIRAM, then any 8-bit heap.
    g_qr_gray_buf = (uint8_t *)heap_caps_malloc(need, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (g_qr_gray_buf == NULL)
    {
        g_qr_gray_buf = (uint8_t *)heap_caps_malloc(need, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
        if (g_qr_gray_buf != NULL)
        {
            Uart_Send_Data((uint8_t *)"[QR] gray buf in psram\r\n", 24);
        }
    }

    if (g_qr_gray_buf == NULL)
    {
        g_qr_gray_buf = (uint8_t *)heap_caps_malloc(need, MALLOC_CAP_8BIT);
    }

    if (g_qr_gray_buf == NULL)
    {
        Uart_Send_Data((uint8_t *)"[QR] gray buf alloc fail\r\n", 26);
        return false;
    }

    g_qr_gray_buf_size = need;
    return true;
}

static void qr_binarize_inplace(uint8_t *buf, size_t len)
{
    if ((buf == NULL) || (len == 0U))
    {
        return;
    }

    uint32_t sum = 0U;
    for (size_t i = 0; i < len; ++i)
    {
        sum += buf[i];
    }

    uint8_t thr = (uint8_t)(sum / len);
    if (thr < 60U)
    {
        thr = 60U;
    }
    else if (thr > 200U)
    {
        thr = 200U;
    }

    for (size_t i = 0; i < len; ++i)
    {
        buf[i] = (buf[i] >= thr) ? 255U : 0U;
    }
}

static void qr_get_scan_size(uint32_t src_w, uint32_t src_h, uint32_t *dst_w, uint32_t *dst_h)
{
    if ((src_w == 0U) || (src_h == 0U))
    {
        *dst_w = 1U;
        *dst_h = 1U;
        return;
    }

    // Keep as much detail as possible while fitting QR_SCAN_MAX_{W,H}.
    uint64_t num = 1U;
    uint64_t den = 1U;
    if ((src_w > QR_SCAN_MAX_W) || (src_h > QR_SCAN_MAX_H))
    {
        uint64_t rw_num = (uint64_t)QR_SCAN_MAX_W;
        uint64_t rw_den = (uint64_t)src_w;
        uint64_t rh_num = (uint64_t)QR_SCAN_MAX_H;
        uint64_t rh_den = (uint64_t)src_h;
        if ((rw_num * rh_den) <= (rh_num * rw_den))
        {
            num = rw_num;
            den = rw_den;
        }
        else
        {
            num = rh_num;
            den = rh_den;
        }
    }

    uint32_t w = (uint32_t)(((uint64_t)src_w * num) / den);
    uint32_t h = (uint32_t)(((uint64_t)src_h * num) / den);
    if (w == 0U)
    {
        w = 1U;
    }
    if (h == 0U)
    {
        h = 1U;
    }

    *dst_w = w;
    *dst_h = h;
}

static bool qr_prepare_gray_from_frame(const camera_fb_t *frame, uint32_t dst_w, uint32_t dst_h)
{
    if ((frame == NULL) || (frame->buf == NULL) || (g_qr_gray_buf == NULL))
    {
        return false;
    }

    const uint32_t src_w = (uint32_t)frame->width;
    const uint32_t src_h = (uint32_t)frame->height;
    if ((dst_w == 0U) || (dst_h == 0U) || (dst_w > src_w) || (dst_h > src_h))
    {
        return false;
    }

    if (frame->format == PIXFORMAT_GRAYSCALE)
    {
        const uint8_t *src = (const uint8_t *)frame->buf;
        for (uint32_t y = 0; y < dst_h; ++y)
        {
            uint32_t sy = (uint32_t)(((uint64_t)y * src_h) / dst_h);
            for (uint32_t x = 0; x < dst_w; ++x)
            {
                uint32_t sx = (uint32_t)(((uint64_t)x * src_w) / dst_w);
                g_qr_gray_buf[(size_t)y * dst_w + x] = src[(size_t)sy * src_w + sx];
            }
        }
        return true;
    }

    if (frame->format != PIXFORMAT_RGB565)
    {
        if ((++g_qr_format_err_log_div % 20U) == 0U)
        {
            char msg[48] = {0};
            snprintf(msg, sizeof(msg), "[QR] fmt=%d unsupported\r\n", (int)frame->format);
            Uart_Send_Data((uint8_t *)msg, (uint16_t)strlen(msg));
        }
        return false;
    }

    const uint16_t *src = (const uint16_t *)frame->buf;
    for (uint32_t y = 0; y < dst_h; ++y)
    {
        uint32_t sy = (uint32_t)(((uint64_t)y * src_h) / dst_h);
        for (uint32_t x = 0; x < dst_w; ++x)
        {
            uint32_t sx = (uint32_t)(((uint64_t)x * src_w) / dst_w);
            uint16_t p = src[(size_t)sy * src_w + sx];
            uint8_t r = (uint8_t)(((p >> 11) & 0x1F) << 3);
            uint8_t g = (uint8_t)(((p >> 5) & 0x3F) << 2);
            uint8_t b = (uint8_t)((p & 0x1F) << 3);
            g_qr_gray_buf[(size_t)y * dst_w + x] = (uint8_t)((30U * r + 59U * g + 11U * b) / 100U);
        }
    }

    return true;
}

static void qr_handle_payload(const char *payload)
{
        auto qr_send_unlock_with_ack = []() -> bool {
            for (int i = 0; i < QR_UNLOCK_RETRY_COUNT; ++i)
            {
                uint32_t ack_mark = Uart1_Get_AckUnlockTick();
                Uart1_Send_Unlock();

                if (Uart1_Wait_AckUnlock_After(ack_mark, QR_UNLOCK_ACK_WAIT_MS))
                {
                    return true;
                }

                Uart_Send_Data((uint8_t *)"[QR] unlock ack timeout\r\n", 25);
            }

            Uart_Send_Data((uint8_t *)"[QR] unlock ack fail\r\n", 22);
            return false;
        };

    if ((payload == NULL) || (payload[0] == '\0'))
    {
        return;
    }

    // Ignore repeated scans during the current unlock hold window.
    if (g_qr_unlock_active)
    {
        return;
    }

    auto extract_json_field = [](const char *json, const char *key, char *out, size_t out_sz) -> bool {
        if ((json == NULL) || (key == NULL) || (out == NULL) || (out_sz < 2U))
        {
            return false;
        }

        out[0] = '\0';
        char pattern[24] = {0};
        snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
        const char *start = strstr(json, pattern);
        if (start == NULL)
        {
            return false;
        }
        start += strlen(pattern);
        const char *end = strchr(start, '"');
        if ((end == NULL) || (end <= start))
        {
            return false;
        }

        size_t len = (size_t)(end - start);
        if (len >= out_sz)
        {
            len = out_sz - 1U;
        }

        memcpy(out, start, len);
        out[len] = '\0';
        return (len > 0U);
    };

    auto extract_json_int64 = [](const char *json, const char *key, int64_t *out) -> bool {
        if ((json == NULL) || (key == NULL) || (out == NULL))
        {
            return false;
        }

        char pattern[24] = {0};
        snprintf(pattern, sizeof(pattern), "\"%s\":", key);
        const char *start = strstr(json, pattern);
        if (start == NULL)
        {
            return false;
        }
        start += strlen(pattern);
        while ((*start == ' ') || (*start == '\t'))
        {
            start++;
        }

        char *end_ptr = NULL;
        long long value = strtoll(start, &end_ptr, 10);
        if ((end_ptr == start) || (end_ptr == NULL))
        {
            return false;
        }

        *out = (int64_t)value;
        return true;
    };

    char grant_token[128] = {0};
    char lock_id[64] = QR_DEFAULT_LOCK_ID;
    bool parsed_token = false;
#if QR_ALLOW_OFFLINE_WINDOW_FALLBACK
    int64_t start_ms = 0;
    int64_t end_ms = 0;
    bool has_window = false;
#endif

    if ((payload[0] == '{') && (strchr(payload, '}') != NULL))
    {
        parsed_token = extract_json_field(payload, "t", grant_token, sizeof(grant_token));
        (void)extract_json_field(payload, "l", lock_id, sizeof(lock_id));
#if QR_ALLOW_OFFLINE_WINDOW_FALLBACK
        has_window = extract_json_int64(payload, "s", &start_ms) && extract_json_int64(payload, "e", &end_ms);
#endif
    }
    else
    {
        size_t raw_len = strlen(payload);
        if (raw_len >= sizeof(grant_token))
        {
            raw_len = sizeof(grant_token) - 1U;
        }
        memcpy(grant_token, payload, raw_len);
        grant_token[raw_len] = '\0';
        parsed_token = (raw_len > 0U);
    }

    if (parsed_token)
    {
        app_verify_result_t verify_rs = app_mywifi_verify_grant_ex(grant_token, lock_id);
        if (verify_rs == APP_VERIFY_RESULT_OK)
        {
            if (qr_token_is_used(grant_token))
            {
                Uart_Send_Data((uint8_t *)"[QR] token already used(local)\r\n", 32);
                return;
            }

            if (!qr_send_unlock_with_ack())
            {
                return;
            }
            Uart_Send_Data((uint8_t *)"[QR] verify ok unlock\r\n", 23);
            qr_token_mark_used(grant_token);
            g_qr_unlock_active = true;
            g_qr_unlock_deadline_us = esp_timer_get_time() + ((int64_t)QR_UNLOCK_HOLD_MS * 1000);
            return;
        }

#if QR_ALLOW_OFFLINE_WINDOW_FALLBACK
        if ((verify_rs == APP_VERIFY_RESULT_NET_ERR) && has_window)
        {
            if (qr_token_is_used(grant_token))
            {
                Uart_Send_Data((uint8_t *)"[QR] token already used(local)\r\n", 32);
                return;
            }

            int64_t now_ms = (int64_t)time(NULL) * 1000;
            if ((now_ms >= start_ms) && (now_ms < end_ms))
            {
                if (!qr_send_unlock_with_ack())
                {
                    return;
                }
                Uart_Send_Data((uint8_t *)"[QR] net fail, local window unlock\r\n", 36);
                qr_token_mark_used(grant_token);
                g_qr_unlock_active = true;
                g_qr_unlock_deadline_us = esp_timer_get_time() + ((int64_t)QR_UNLOCK_HOLD_MS * 1000);
                return;
            }

            {
                char ts_msg[96] = {0};
                int n = snprintf(ts_msg, sizeof(ts_msg), "[QR] local window miss now=%lld s=%lld e=%lld\r\n",
                                 (long long)now_ms,
                                 (long long)start_ms,
                                 (long long)end_ms);
                if (n > 0)
                {
                    Uart_Send_Data((uint8_t *)ts_msg, (uint16_t)n);
                }
            }
        }
#else
        if (verify_rs == APP_VERIFY_RESULT_NET_ERR)
        {
            Uart_Send_Data((uint8_t *)"[QR] net fail, reject (cloud required)\r\n", 40);
        }
#endif

        Uart_Send_Data((uint8_t *)"[QR] verify reject\r\n", 20);
        if (strcmp(payload, QR_UNLOCK_PAYLOAD) != 0)
        {
            return;
        }
    }

    if (strcmp(payload, QR_UNLOCK_PAYLOAD) == 0)
    {
        if (!qr_send_unlock_with_ack())
        {
            return;
        }
        Uart_Send_Data((uint8_t *)"[QR] unlock\r\n", 13);
        g_qr_unlock_active = true;
        g_qr_unlock_deadline_us = esp_timer_get_time() + ((int64_t)QR_UNLOCK_HOLD_MS * 1000);
    }
}

static bool qr_scan_and_extract_payload(camera_fb_t *frame, char *payload_out, size_t payload_out_sz)
{
    if ((frame == NULL) || (payload_out == NULL) || (payload_out_sz < 2U))
    {
        return false;
    }

    payload_out[0] = '\0';

    int64_t now_us = esp_timer_get_time();
    if ((now_us - g_qr_last_scan_us) < QR_SCAN_MIN_INTERVAL_US)
    {
        return false;
    }
    g_qr_last_scan_us = now_us;

    esp_image_scanner_t *scanner = esp_code_scanner_create();
    if (scanner == NULL)
    {
        Uart_Send_Data((uint8_t *)"[QR] scanner create fail\r\n", 26);
        return false;
    }

    esp_code_scanner_config_t cfg = {
        .mode = ESP_CODE_SCANNER_MODE_FAST,
        .fmt = ESP_CODE_SCANNER_IMAGE_RGB565,
        .width = (uint32_t)frame->width,
        .height = (uint32_t)frame->height,
    };
    if (esp_code_scanner_set_config(scanner, cfg) != ESP_OK)
    {
        esp_code_scanner_destroy(scanner);
        Uart_Send_Data((uint8_t *)"[QR] config fail\r\n", 18);
        return false;
    }

    int decoded_num = esp_code_scanner_scan_image(scanner, frame->buf);

    if (decoded_num <= 0)
    {
        esp_code_scanner_destroy(scanner);
        if ((++g_qr_no_result_log_div % 8U) == 0U)
        {
            char msg[80] = {0};
            snprintf(msg, sizeof(msg), "[QR] no result sz=%lux%lu fmt=%d\r\n",
                     (unsigned long)frame->width,
                     (unsigned long)frame->height,
                     (int)frame->format);
            Uart_Send_Data((uint8_t *)msg, (uint16_t)strlen(msg));
        }
        return false;
    }
    g_qr_no_result_log_div = 0;

    esp_code_scanner_symbol_t sym = esp_code_scanner_result(scanner);
    if ((sym.data == NULL) || (sym.datalen == 0U))
    {
        esp_code_scanner_destroy(scanner);
        Uart_Send_Data((uint8_t *)"[QR] result empty\r\n", 19);
        return false;
    }

    uint32_t copy_len = sym.datalen;
    if (copy_len >= sizeof(g_last_qr_payload))
    {
        copy_len = sizeof(g_last_qr_payload) - 1U;
    }

    char payload[128] = {0};
    memcpy(payload, sym.data, copy_len);
    payload[copy_len] = '\0';
    esp_code_scanner_destroy(scanner);

    bool is_same = (strncmp(g_last_qr_payload, payload, sizeof(g_last_qr_payload) - 1) == 0);
    if (is_same && ((now_us - g_last_qr_report_us) < ((int64_t)QR_REPORT_DEDUP_MS * 1000)))
    {
        Uart_Send_Data((uint8_t *)"[QR] dedup\r\n", 12);
        return false;
    }

    snprintf(g_last_qr_payload, sizeof(g_last_qr_payload), "%s", payload);
    g_last_qr_report_us = now_us;

    char msg[220] = {0};
    snprintf(msg, sizeof(msg), "[QR] %s\r\n", payload);
    Uart_Send_Data((uint8_t *)msg, (uint16_t)strlen(msg));

    size_t out_len = strlen(payload);
    if (out_len >= payload_out_sz)
    {
        out_len = payload_out_sz - 1U;
    }
    memcpy(payload_out, payload, out_len);
    payload_out[out_len] = '\0';
    return true;
}

static void face_update_rearm_on_no_face(void)
{
    if (!g_face_wait_rearm)
    {
        return;
    }

    if (g_face_rearm_noface_count < 255U)
    {
        g_face_rearm_noface_count++;
    }

    if (g_face_rearm_noface_count >= FACE_REARM_NOFACE_COUNT)
    {
        g_face_wait_rearm = false;
        g_face_rearm_noface_count = 0;
        g_face_rearm_deadline_us = 0;
        Uart_Send_Data((uint8_t *)"[FACE] rearm ready\r\n", 20);
    }
}

static void face_try_rearm_timeout(void)
{
    if (!g_face_wait_rearm)
    {
        return;
    }

    if ((g_face_rearm_deadline_us > 0) && (esp_timer_get_time() >= g_face_rearm_deadline_us))
    {
        g_face_wait_rearm = false;
        g_face_rearm_noface_count = 0;
        g_face_rearm_deadline_us = 0;
        Uart_Send_Data((uint8_t *)"[FACE] rearm timeout\r\n", 22);
    }
}

static void face_try_auto_lock(void)
{
    if (!g_face_unlock_active)
    {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    // Keep unlock for a stable visible window; do not relock immediately on transient face loss.
    if (now_us >= g_face_unlock_deadline_us)
    {
        Uart1_Send_Lock();
        Uart_Send_Data((uint8_t *)"[FACE] lock\r\n", 13);
        g_face_unlock_active = false;
        g_face_lost_count = 0;
        g_face_wait_rearm = true;
        g_face_rearm_noface_count = 0;
        g_face_rearm_deadline_us = now_us + ((int64_t)FACE_REARM_HOLD_MS * 1000);
    }
}

static void qr_try_auto_lock(void)
{
    if (!g_qr_unlock_active)
    {
        return;
    }

    int64_t now_us = esp_timer_get_time();
    if (now_us >= g_qr_unlock_deadline_us)
    {
        Uart1_Send_Lock();
        Uart_Send_Data((uint8_t *)"[QR] lock\r\n", 11);
        g_qr_unlock_active = false;
    }
}

static void rgb_print(camera_fb_t *fb, uint32_t color, const char *str)
{
    fb_gfx_print(fb, (fb->width - (strlen(str) * 14)) / 2, 10, color, str);
}

static int rgb_printf(camera_fb_t *fb, uint32_t color, const char *format, ...)
{
    char loc_buf[64];
    char *temp = loc_buf;
    int len;
    va_list arg;
    va_list copy;
    va_start(arg, format);
    va_copy(copy, arg);
    len = vsnprintf(loc_buf, sizeof(loc_buf), format, arg);
    va_end(copy);
    if (len >= sizeof(loc_buf))
    {
        temp = (char *)malloc(len + 1);
        if (temp == NULL)
        {
            return 0;
        }
    }
    vsnprintf(temp, len + 1, format, arg);
    va_end(arg);
    rgb_print(fb, color, temp);
    if (len > 64)
    {
        free(temp);
    }
    return len;
}

static void task_process_handler(void *arg)
{
    camera_fb_t *frame = NULL;
    // Slightly relaxed thresholds for better detection in indoor/low-light demo scenes.
    HumanFaceDetectMSR01 detector(0.20F, 0.35F, 10, 0.20F);
    HumanFaceDetectMNP01 detector2(0.25F, 0.35F, 10);

#if CONFIG_MFN_V1
#if CONFIG_S8
    FaceRecognition112V1S8 *recognizer = new FaceRecognition112V1S8();
#elif CONFIG_S16
    FaceRecognition112V1S16 *recognizer = new FaceRecognition112V1S16();
#endif
#endif
    show_state_t frame_show_state = SHOW_STATE_IDLE;
    recognizer_state_t _gEvent;
    recognizer->set_partition(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "fr");
    int partition_result = recognizer->set_ids_from_flash();
    {
        char msg[64] = {0};
        snprintf(msg, sizeof(msg), "[FACE] enrolled=%d\r\n", recognizer->get_enrolled_id_num());
        Uart_Send_Data((uint8_t *)msg, (uint16_t)strlen(msg));
    }

    while (true)
    {
        uint8_t face_data[15]="\0";
        bool frame_from_direct = false;
        xSemaphoreTake(xMutex, portMAX_DELAY);
        _gEvent = gEvent;
        xSemaphoreGive(xMutex);

        

        if (_gEvent)
        {
            // QR mode is handled by a dedicated task to avoid heavy mixed workload in one loop.
            if (_gEvent == QR_SCAN)
            {
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }

            bool is_detected = false;

            if ((xQueueFrameI != NULL) && xQueueReceive(xQueueFrameI, &frame, pdMS_TO_TICKS(600)))
            {
                std::list<dl::detect::result_t> detect_results;
                if (_gEvent != QR_SCAN)
                {
                    std::list<dl::detect::result_t> &detect_candidates = detector.infer((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3});
                    detect_results = detector2.infer((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3}, detect_candidates);
                    if ((++g_det_log_div % 30U) == 0U)
                    {
                        char msg[48] = {0};
                        snprintf(msg, sizeof(msg), "[FACE] det=%u\r\n", (unsigned)detect_results.size());
                        Uart_Send_Data((uint8_t *)msg, (uint16_t)strlen(msg));
                    }

                    // Accept at least one detected face; strict ==1 can miss valid scenes with extra candidates.
                    if (detect_results.size() > 0)
                        is_detected = true;
                }

                if (!is_detected && (_gEvent == RECOGNIZE))
                {
                    if ((++g_no_face_log_div % 30U) == 0U)
                    {
                        Uart_Send_Data((uint8_t *)"[FACE] no face\r\n", 16);
                    }

                    if (g_face_unlock_active && (g_face_lost_count < 255U))
                    {
                        g_face_lost_count++;
                    }
                    face_update_rearm_on_no_face();
                    face_try_auto_lock();
                }

                if (is_detected || (_gEvent == QR_SCAN))
                {
                    switch (_gEvent)
                    {
                    case ENROLL:
                    {
                        int64_t now_us = esp_timer_get_time();
                        if ((now_us - g_last_enroll_us) < ((int64_t)FACE_ENROLL_COOLDOWN_MS * 1000))
                        {
                            break;
                        }

                        face_info_t precheck = recognizer->recognize((uint16_t *)frame->buf,
                                                                     {(int)frame->height, (int)frame->width, 3},
                                                                     detect_results.front().keypoint);
                        if ((precheck.id > 0) && (precheck.similarity >= FACE_ENROLL_DUP_SIM_THRESHOLD))
                        {
                            char msg[80] = {0};
                            snprintf(msg, sizeof(msg), "[FACE] already enrolled id=%d sim=%.2f\r\n", precheck.id, precheck.similarity);
                            Uart_Send_Data((uint8_t *)msg, (uint16_t)strlen(msg));

                            xSemaphoreTake(xMutex, portMAX_DELAY);
                            gEvent = IDLE;
                            xSemaphoreGive(xMutex);
                            Uart_Send_Data((uint8_t *)"[FACE] mode->IDLE\r\n", 19);
                            break;
                        }

                        recognizer->enroll_id((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3}, detect_results.front().keypoint, "", true);
                        g_last_enroll_us = now_us;
                        ESP_LOGW("ENROLL", "ID %d is enrolled", recognizer->get_enrolled_ids().back().id);
                        {
                            char msg[64] = {0};
                            snprintf(msg, sizeof(msg), "[FACE] enrolled new id=%d\r\n", recognizer->get_enrolled_ids().back().id);
                            Uart_Send_Data((uint8_t *)msg, (uint16_t)strlen(msg));
                        }
                        xSemaphoreTake(xMutex, portMAX_DELAY);
                        gEvent = IDLE;
                        xSemaphoreGive(xMutex);
                        Uart_Send_Data((uint8_t *)"[FACE] mode->IDLE\r\n", 19);
                        frame_show_state = SHOW_STATE_ENROLL;
                        break;
                    }

                    case RECOGNIZE:
                        recognize_result = recognizer->recognize((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3}, detect_results.front().keypoint);
                        print_detection_result(detect_results);
                        if (recognize_result.id > 0)
                        {
                            ESP_LOGI("RECOGNIZE", "Similarity: %f, Match ID: %d", recognize_result.similarity, recognize_result.id);
                            sprintf((char*)face_data,"@ID:%d!\r\n",recognize_result.id); //左上角坐标 右下角坐标
                            Uart_Send_Data(face_data,strlen((char*)face_data)); 

                            face_try_rearm_timeout();

                            if (g_face_wait_rearm)
                            {
                                if ((++g_no_face_log_div % 60U) == 0U)
                                {
                                    Uart_Send_Data((uint8_t *)"[FACE] wait leave\r\n", 19);
                                }
                            }
                            else if (!g_face_unlock_active)
                            {
                                Uart1_Send_Unlock();
                                Uart_Send_Data((uint8_t *)"[FACE] unlock\r\n", 15);
                                g_face_unlock_active = true;
                                g_face_unlock_deadline_us = esp_timer_get_time() + ((int64_t)FACE_UNLOCK_HOLD_MS * 1000);
                            }

                            g_face_lost_count = 0;
                            g_face_rearm_noface_count = 0;

                            //同时i2c获取到识别的id.
                            set_IIC_data_id(recognize_result.id); //为正数
                        }
                            
                        else
                        {
                            ESP_LOGE("RECOGNIZE", "Similarity: %f, Match ID: %d", recognize_result.similarity, recognize_result.id);
                            sprintf((char*)face_data,"@ID:%d!\r\n",recognize_result.id); //左上角坐标 右下角坐标
                            Uart_Send_Data(face_data,strlen((char*)face_data)); 
                            {
                                char msg[64] = {0};
                                snprintf(msg, sizeof(msg), "[FACE] unknown sim=%.2f\r\n", recognize_result.similarity);
                                Uart_Send_Data((uint8_t *)msg, (uint16_t)strlen(msg));
                            }

                            if (g_face_unlock_active && (g_face_lost_count < 255U))
                            {
                                g_face_lost_count++;
                            }
                            face_update_rearm_on_no_face();

                            //同时i2c获取到识别的id.
                            set_IIC_data_id(recognize_result.id); //为负数
                        }

                        face_try_auto_lock();

                        // One-shot behavior: stop face scanning after one recognition attempt.
                        xSemaphoreTake(xMutex, portMAX_DELAY);
                        gEvent = IDLE;
                        xSemaphoreGive(xMutex);
                        Uart_Send_Data((uint8_t *)"[FACE] oneshot->IDLE\r\n", 22);
                            
                        frame_show_state = SHOW_STATE_RECOGNIZE;
                        break;

                    case QR_SCAN:
                        // QR scanning is handled by task_qr_process_handler.
                        frame_show_state = SHOW_STATE_IDLE;
                        break;

                    case DELETE:
                        vTaskDelay(10);
                        recognizer->delete_id(true);
                        ESP_LOGE("DELETE", "% d IDs left", recognizer->get_enrolled_id_num());
                        xSemaphoreTake(xMutex, portMAX_DELAY);
                        gEvent = IDLE;
                        xSemaphoreGive(xMutex);
                        Uart_Send_Data((uint8_t *)"[FACE] mode->IDLE\r\n", 19);
                        frame_show_state = SHOW_STATE_DELETE;
                        break;

                    default:
                        break;
                    }
                }

                if (frame_show_state != SHOW_STATE_IDLE)
                {
                    static int frame_count = 0;
                    switch (frame_show_state)
                    {
                    case SHOW_STATE_DELETE:
                        rgb_printf(frame, RGB565_MASK_RED, "%d IDs left", recognizer->get_enrolled_id_num());
                        break;

                    case SHOW_STATE_RECOGNIZE:
                        if (recognize_result.id > 0)
                            // if(recognize_result.id ==1)//只为拍摄，后面直接删
                            // {
                            //     rgb_printf(frame, RGB565_MASK_GREEN, "ID:zhangsan");
                            // }
                            // else if (recognize_result.id ==2)
                            // {
                            //     rgb_printf(frame, RGB565_MASK_GREEN, "ID:lisi");
                            // }
                            // else if (recognize_result.id ==3)
                            // {
                            //     rgb_printf(frame, RGB565_MASK_GREEN, "ID:huangwu");
                            // }
                            // else
                                rgb_printf(frame, RGB565_MASK_GREEN, "ID %d", recognize_result.id);
                        else
                            rgb_print(frame, RGB565_MASK_RED, "who ?");
                        break;

                    case SHOW_STATE_ENROLL:
                        rgb_printf(frame, RGB565_MASK_BLUE, "Enroll: ID %d", recognizer->get_enrolled_ids().back().id);
                        break;

                    default:
                        break;
                    }

                    if (++frame_count > FRAME_DELAY_NUM)
                    {
                        frame_count = 0;
                        frame_show_state = SHOW_STATE_IDLE;
                    }
                }

                if ((_gEvent != QR_SCAN) && detect_results.size())
                {
#if !CONFIG_IDF_TARGET_ESP32S3
                    print_detection_result(detect_results);
#endif
                    draw_detection_result((uint16_t *)frame->buf, frame->height, frame->width, detect_results);
                }
            }
            else
            {
                // Fallback path: pull frame directly from camera driver.
                frame = esp_camera_fb_get();
                if (frame != NULL)
                {
                    frame_from_direct = true;
                    if ((_gEvent != QR_SCAN) && ((++g_no_frame_log_div % 10U) == 0U))
                    {
                        Uart_Send_Data((uint8_t *)"[FACE] frame from direct\r\n", 26);
                    }
                }
                else if ((_gEvent != QR_SCAN) && ((++g_no_frame_log_div % 5U) == 0U))
                {
                    Uart_Send_Data((uint8_t *)"[FACE] no frame\r\n", 17);
                    face_try_auto_lock();
                    continue;
                }

                if (frame == NULL)
                {
                    face_try_auto_lock();
                    continue;
                }

                std::list<dl::detect::result_t> detect_results;
                if (_gEvent != QR_SCAN)
                {
                    std::list<dl::detect::result_t> &detect_candidates = detector.infer((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3});
                    detect_results = detector2.infer((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3}, detect_candidates);
                    if ((++g_det_log_div % 30U) == 0U)
                    {
                        char msg[48] = {0};
                        snprintf(msg, sizeof(msg), "[FACE] det=%u\r\n", (unsigned)detect_results.size());
                        Uart_Send_Data((uint8_t *)msg, (uint16_t)strlen(msg));
                    }

                    if (detect_results.size() > 0)
                        is_detected = true;
                }

                if (!is_detected && (_gEvent == RECOGNIZE))
                {
                    if ((++g_no_face_log_div % 30U) == 0U)
                    {
                        Uart_Send_Data((uint8_t *)"[FACE] no face\r\n", 16);
                    }

                    if (g_face_unlock_active && (g_face_lost_count < 255U))
                    {
                        g_face_lost_count++;
                    }
                    face_update_rearm_on_no_face();
                    face_try_auto_lock();
                }

                if (is_detected || (_gEvent == QR_SCAN))
                {
                    switch (_gEvent)
                    {
                    case ENROLL:
                    {
                        int64_t now_us = esp_timer_get_time();
                        if ((now_us - g_last_enroll_us) < ((int64_t)FACE_ENROLL_COOLDOWN_MS * 1000))
                        {
                            break;
                        }

                        face_info_t precheck = recognizer->recognize((uint16_t *)frame->buf,
                                                                     {(int)frame->height, (int)frame->width, 3},
                                                                     detect_results.front().keypoint);
                        if ((precheck.id > 0) && (precheck.similarity >= FACE_ENROLL_DUP_SIM_THRESHOLD))
                        {
                            char msg[80] = {0};
                            snprintf(msg, sizeof(msg), "[FACE] already enrolled id=%d sim=%.2f\r\n", precheck.id, precheck.similarity);
                            Uart_Send_Data((uint8_t *)msg, (uint16_t)strlen(msg));

                            xSemaphoreTake(xMutex, portMAX_DELAY);
                            gEvent = IDLE;
                            xSemaphoreGive(xMutex);
                            Uart_Send_Data((uint8_t *)"[FACE] mode->IDLE\r\n", 19);
                            break;
                        }

                        recognizer->enroll_id((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3}, detect_results.front().keypoint, "", true);
                        g_last_enroll_us = now_us;
                        ESP_LOGW("ENROLL", "ID %d is enrolled", recognizer->get_enrolled_ids().back().id);
                        {
                            char msg[64] = {0};
                            snprintf(msg, sizeof(msg), "[FACE] enrolled new id=%d\r\n", recognizer->get_enrolled_ids().back().id);
                            Uart_Send_Data((uint8_t *)msg, (uint16_t)strlen(msg));
                        }
                        xSemaphoreTake(xMutex, portMAX_DELAY);
                        gEvent = IDLE;
                        xSemaphoreGive(xMutex);
                        Uart_Send_Data((uint8_t *)"[FACE] mode->IDLE\r\n", 19);
                        frame_show_state = SHOW_STATE_ENROLL;
                        break;
                    }

                    case RECOGNIZE:
                        recognize_result = recognizer->recognize((uint16_t *)frame->buf, {(int)frame->height, (int)frame->width, 3}, detect_results.front().keypoint);
                        print_detection_result(detect_results);
                        if (recognize_result.id > 0)
                        {
                            ESP_LOGI("RECOGNIZE", "Similarity: %f, Match ID: %d", recognize_result.similarity, recognize_result.id);
                            sprintf((char*)face_data,"@ID:%d!\r\n",recognize_result.id);
                            Uart_Send_Data(face_data,strlen((char*)face_data));

                            face_try_rearm_timeout();

                            if (g_face_wait_rearm)
                            {
                                if ((++g_no_face_log_div % 60U) == 0U)
                                {
                                    Uart_Send_Data((uint8_t *)"[FACE] wait leave\r\n", 19);
                                }
                            }
                            else if (!g_face_unlock_active)
                            {
                                Uart1_Send_Unlock();
                                Uart_Send_Data((uint8_t *)"[FACE] unlock\r\n", 15);
                                g_face_unlock_active = true;
                                g_face_unlock_deadline_us = esp_timer_get_time() + ((int64_t)FACE_UNLOCK_HOLD_MS * 1000);
                            }

                            g_face_lost_count = 0;
                            g_face_rearm_noface_count = 0;
                            set_IIC_data_id(recognize_result.id);
                        }
                        else
                        {
                            ESP_LOGE("RECOGNIZE", "Similarity: %f, Match ID: %d", recognize_result.similarity, recognize_result.id);
                            sprintf((char*)face_data,"@ID:%d!\r\n",recognize_result.id);
                            Uart_Send_Data(face_data,strlen((char*)face_data));
                            {
                                char msg[64] = {0};
                                snprintf(msg, sizeof(msg), "[FACE] unknown sim=%.2f\r\n", recognize_result.similarity);
                                Uart_Send_Data((uint8_t *)msg, (uint16_t)strlen(msg));
                            }

                            if (g_face_unlock_active && (g_face_lost_count < 255U))
                            {
                                g_face_lost_count++;
                            }
                            face_update_rearm_on_no_face();

                            set_IIC_data_id(recognize_result.id);
                        }

                        face_try_auto_lock();

                        // One-shot behavior: stop face scanning after one recognition attempt.
                        xSemaphoreTake(xMutex, portMAX_DELAY);
                        gEvent = IDLE;
                        xSemaphoreGive(xMutex);
                        Uart_Send_Data((uint8_t *)"[FACE] oneshot->IDLE\r\n", 22);

                        frame_show_state = SHOW_STATE_RECOGNIZE;
                        break;

                    case QR_SCAN:
                        // QR scanning is handled by task_qr_process_handler.
                        frame_show_state = SHOW_STATE_IDLE;
                        break;

                    case DELETE:
                        vTaskDelay(10);
                        recognizer->delete_id(true);
                        ESP_LOGE("DELETE", "% d IDs left", recognizer->get_enrolled_id_num());
                        xSemaphoreTake(xMutex, portMAX_DELAY);
                        gEvent = IDLE;
                        xSemaphoreGive(xMutex);
                        Uart_Send_Data((uint8_t *)"[FACE] mode->IDLE\r\n", 19);
                        frame_show_state = SHOW_STATE_DELETE;
                        break;

                    default:
                        break;
                    }
                }

                if (frame_show_state != SHOW_STATE_IDLE)
                {
                    static int frame_count = 0;
                    switch (frame_show_state)
                    {
                    case SHOW_STATE_DELETE:
                        rgb_printf(frame, RGB565_MASK_RED, "%d IDs left", recognizer->get_enrolled_id_num());
                        break;
                    case SHOW_STATE_RECOGNIZE:
                        if (recognize_result.id > 0)
                            rgb_printf(frame, RGB565_MASK_GREEN, "ID %d", recognize_result.id);
                        else
                            rgb_print(frame, RGB565_MASK_RED, "who ?");
                        break;
                    case SHOW_STATE_ENROLL:
                        rgb_printf(frame, RGB565_MASK_BLUE, "Enroll: ID %d", recognizer->get_enrolled_ids().back().id);
                        break;
                    default:
                        break;
                    }

                    if (++frame_count > FRAME_DELAY_NUM)
                    {
                        frame_count = 0;
                        frame_show_state = SHOW_STATE_IDLE;
                    }
                }

                if ((_gEvent != QR_SCAN) && detect_results.size())
                {
#if !CONFIG_IDF_TARGET_ESP32S3
                    print_detection_result(detect_results);
#endif
                    draw_detection_result((uint16_t *)frame->buf, frame->height, frame->width, detect_results);
                }
            }

            if (xQueueFrameO)
            {
                if (!frame_from_direct && (xQueueSend(xQueueFrameO, &frame, 0) == pdPASS))
                {
                    // handed to output queue
                }
                else
                {
                    esp_camera_fb_return(frame);
                }
            }
            else if (gReturnFB)
            {
                esp_camera_fb_return(frame);
            }
            else
            {
                free(frame);
            }

            if (xQueueResult && is_detected)
            {
                xQueueSend(xQueueResult, &recognize_result, portMAX_DELAY);
            }

            // Slight throttling lowers chip temperature while keeping UI smooth.
            vTaskDelay(pdMS_TO_TICKS(15));
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(30));
        }
    }
}

static void task_qr_process_handler(void *arg)
{
    while (true)
    {
        // Keep lock timeout active even when face mode is switched away (e.g. IDLE).
        face_try_auto_lock();
        qr_try_auto_lock();

        recognizer_state_t mode;
        xSemaphoreTake(xMutex, portMAX_DELAY);
        mode = gEvent;
        xSemaphoreGive(xMutex);

        if (mode != QR_SCAN)
        {
            vTaskDelay(pdMS_TO_TICKS(80));
            continue;
        }

        camera_fb_t *frame = esp_camera_fb_get();
        if (frame == NULL)
        {
            if ((++g_qr_no_frame_log_div % 30U) == 0U)
            {
                Uart_Send_Data((uint8_t *)"[QR] no frame\r\n", 15);
            }
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }

        char qr_payload[128] = {0};
        bool has_payload = qr_scan_and_extract_payload(frame, qr_payload, sizeof(qr_payload));
        esp_camera_fb_return(frame);
        if (has_payload)
        {
            qr_handle_payload(qr_payload);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void task_event_handler(void *arg)
{
    recognizer_state_t _gEvent;
    while (true)
    {
        xQueueReceive(xQueueEvent, &(_gEvent), portMAX_DELAY);

        ESP_LOGI(TAG, "Mode switch: %d", (int)_gEvent);

        if (_gEvent == QR_SCAN)
        {
            camera_switch_mode(true);
            g_qr_last_scan_us = 0;
            g_qr_no_result_log_div = 0;
            g_qr_format_err_log_div = 0;
            Uart_Send_Data((uint8_t *)"[QR] scanner ready\r\n", 20);
            Uart_Send_Data((uint8_t *)"[MODE] QR\r\n", 11);
        }
        else if (_gEvent == RECOGNIZE)
        {
            camera_switch_mode(false);
            Uart_Send_Data((uint8_t *)"[MODE] FACE\r\n", 13);
        }
        else if ((_gEvent == IDLE) || (_gEvent == DETECT))
        {
            camera_switch_mode(false);
            Uart_Send_Data((uint8_t *)"[MODE] IDLE\r\n", 13);
        }

        if ((_gEvent != RECOGNIZE) && g_face_unlock_active)
        {
            int64_t now_us = esp_timer_get_time();
            if (now_us >= g_face_unlock_deadline_us)
            {
                Uart1_Send_Lock();
                Uart_Send_Data((uint8_t *)"[FACE] lock(mode)\r\n", 19);
                g_face_unlock_active = false;
                g_face_lost_count = 0;
                g_face_wait_rearm = true;
                g_face_rearm_noface_count = 0;
                g_face_rearm_deadline_us = now_us + ((int64_t)FACE_REARM_HOLD_MS * 1000);
            }
            else
            {
                Uart_Send_Data((uint8_t *)"[FACE] keep unlock\r\n", 20);
            }
        }

        xSemaphoreTake(xMutex, portMAX_DELAY);
        gEvent = _gEvent;
        xSemaphoreGive(xMutex);
    }
}

void register_human_face_recognition(const QueueHandle_t frame_i,
                                     const QueueHandle_t event,
                                     const QueueHandle_t result,
                                     const QueueHandle_t frame_o,
                                     const bool camera_fb_return)
{
    xQueueFrameI = frame_i;
    xQueueFrameO = frame_o;
    xQueueEvent = event;
    xQueueResult = result;
    gReturnFB = camera_fb_return;
    xMutex = xSemaphoreCreateMutex();

    BaseType_t task_ret = xTaskCreatePinnedToCore(task_process_handler, TAG, 12 * 1024, NULL, 5, NULL, 0);//核心0
    if (task_ret == pdPASS)
    {
        Uart_Send_Data((uint8_t *)"[FACE] task create ok\r\n", 23);
    }
    else
    {
        Uart_Send_Data((uint8_t *)"[FACE] task create fail\r\n", 25);
    }

    if (xQueueEvent)
    {
        BaseType_t event_ret = xTaskCreatePinnedToCore(task_event_handler, TAG, 4 * 1024, NULL, 5, NULL, 1);//5
        if (event_ret == pdPASS)
        {
            Uart_Send_Data((uint8_t *)"[FACE] event task ok\r\n", 22);
        }
        else
        {
            Uart_Send_Data((uint8_t *)"[FACE] event task fail\r\n", 24);
        }
    }

    BaseType_t qr_ret = xTaskCreatePinnedToCore(task_qr_process_handler, "qr_scan_task", 12 * 1024, NULL, 3, NULL, 1);
    if (qr_ret == pdPASS)
    {
        Uart_Send_Data((uint8_t *)"[QR] task create ok\r\n", 21);
    }
    else
    {
        Uart_Send_Data((uint8_t *)"[QR] task create fail\r\n", 23);
    }
}
