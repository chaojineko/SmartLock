#include "main.h"
#include "usart.h"
#include "gpio.h"
#include "string.h"
#include "stdio.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"
#include "sfm.h"

// FPM383C UART protocol notes:
// Frame = Header(8) + Len(2) + HeaderChk(1) + Content(Len)
// Content = Password(4) + Cmd(2) + Status/Data + ContentChk(1)
// Checksum = two's complement of byte-sum.

extern UART_HandleTypeDef huart2;

volatile uint8_t  g_usart2_rx_buf[512];
volatile uint32_t g_usart2_rx_cnt = 0;
volatile uint32_t g_usart2_rx_end = 0;

static uint32_t g_sfm_active_baud = 0;
static uint32_t g_sfm_password = 0x00000000UL;
static SFM_State_t g_sfm_state = SFM_STATE_IDLE;

#define FPM383_HEADER_LEN        8U
#define FPM383_MAX_CONTENT_LEN   256U

static const uint8_t g_fpm383_header[FPM383_HEADER_LEN] = {
    0xF1, 0x1F, 0xE2, 0x2E, 0xB6, 0x6B, 0xA8, 0x8A
};

typedef struct {
    uint8_t cmd1;
    uint8_t cmd2;
    uint32_t status;
    uint8_t data[128];
    uint16_t data_len;
} fpm383_resp_t;

static int32_t sfm_request(uint8_t cmd1, uint8_t cmd2,
                           const uint8_t *payload, uint16_t payload_len,
                           fpm383_resp_t *resp, uint32_t timeout_ms);

static uint8_t fpm383_checksum(const uint8_t *data, uint16_t len)
{
    uint32_t sum = 0;
    uint16_t i;

    for (i = 0; i < len; i++)
    {
        sum += data[i];
    }

    return (uint8_t)((~sum + 1U) & 0xFFU);
}

uint8_t bcc_check(uint8_t *buf, uint32_t len)
{
    // Keep symbol for compatibility; FPM383C uses two's complement checksum.
    return fpm383_checksum(buf, (uint16_t)len);
}

static int sfm_uart_read_exact(uint8_t *buf, uint16_t len, uint32_t timeout_ms)
{
    uint16_t pos = 0;
    uint32_t start = HAL_GetTick();

    while (pos < len)
    {
        if ((HAL_GetTick() - start) >= timeout_ms)
        {
            return 0;
        }

        if (HAL_UART_Receive(&huart2, &buf[pos], 1, 10) == HAL_OK)
        {
            pos++;
        }
    }

    return 1;
}

static void sfm_uart_drain(void)
{
    uint8_t b;
    uint16_t n = 0;

    // Drop stale bytes from previous async responses.
    while (n < 256U)
    {
        if (HAL_UART_Receive(&huart2, &b, 1, 2) != HAL_OK)
        {
            break;
        }
        n++;
    }
}

static int sfm_uart_find_header(uint32_t timeout_ms)
{
    uint8_t b;
    uint8_t idx = 0;
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < timeout_ms)
    {
        if (HAL_UART_Receive(&huart2, &b, 1, 10) != HAL_OK)
        {
            continue;
        }

        if (b == g_fpm383_header[idx])
        {
            idx++;
            if (idx >= FPM383_HEADER_LEN)
            {
                return 1;
            }
        }
        else
        {
            idx = (b == g_fpm383_header[0]) ? 1U : 0U;
        }
    }

    return 0;
}

static int32_t sfm_status_to_ack(uint32_t status)
{
    uint8_t code = (uint8_t)(status & 0xFFU);

    if (status == 0U)
    {
        return SFM_ACK_SUCCESS;
    }

    switch (code)
    {
        case 0x08: return SFM_ACK_TIMEOUT;
        case 0x05: return SFM_ACK_NOUSER;
        case 0x07: return SFM_ACK_USER_EXIST;
        case 0x0A: return SFM_ACK_HARDWAREERROR;
        case 0x0B: return SFM_ACK_FULL;
        case 0x0E: return SFM_ACK_IMAGEERROR;
        case 0x0F: return SFM_ACK_USER_EXIST;
        case 0x10: return SFM_ACK_ALGORITHMFAIL;
        default:   return SFM_ACK_FAIL;
    }
}

static int32_t sfm_probe_ready(uint32_t timeout_ms)
{
    fpm383_resp_t resp;
    return sfm_request(0x03, 0x03, NULL, 0, &resp, timeout_ms);
}

static int sfm_send_packet(uint8_t cmd1, uint8_t cmd2, const uint8_t *payload, uint16_t payload_len)
{
    uint8_t frame[320];
    uint16_t content_len = (uint16_t)(4U + 2U + payload_len + 1U);
    uint16_t frame_len;
    uint16_t i;
    uint16_t idx;

    if (content_len > FPM383_MAX_CONTENT_LEN)
    {
        return 0;
    }

    frame_len = (uint16_t)(FPM383_HEADER_LEN + 2U + 1U + content_len);

    idx = 0;
    for (i = 0; i < FPM383_HEADER_LEN; i++)
    {
        frame[idx++] = g_fpm383_header[i];
    }

    frame[idx++] = (uint8_t)(content_len >> 8);
    frame[idx++] = (uint8_t)(content_len & 0xFFU);
    frame[idx++] = fpm383_checksum(frame, (uint16_t)(FPM383_HEADER_LEN + 2U));

    frame[idx++] = (uint8_t)(g_sfm_password >> 24);
    frame[idx++] = (uint8_t)(g_sfm_password >> 16);
    frame[idx++] = (uint8_t)(g_sfm_password >> 8);
    frame[idx++] = (uint8_t)(g_sfm_password & 0xFFU);

    frame[idx++] = cmd1;
    frame[idx++] = cmd2;

    if ((payload != NULL) && (payload_len > 0U))
    {
        memcpy(&frame[idx], payload, payload_len);
        idx = (uint16_t)(idx + payload_len);
    }

    frame[idx++] = fpm383_checksum(&frame[(FPM383_HEADER_LEN + 2U + 1U)], (uint16_t)(content_len - 1U));

    if (idx != frame_len)
    {
        return 0;
    }

    sfm_uart_drain();

    if (HAL_UART_Transmit(&huart2, frame, frame_len, 200) != HAL_OK)
    {
        return 0;
    }

    return 1;
}

static int sfm_recv_packet(fpm383_resp_t *resp, uint32_t timeout_ms)
{
    uint8_t meta[3];
    uint8_t content[FPM383_MAX_CONTENT_LEN];
    uint8_t header_plus_len[10];
    uint16_t content_len;
    uint8_t expected_header_chk;
    uint8_t expected_content_chk;
    uint8_t parsed_data_len;
    uint16_t raw_len;

    if (resp == NULL)
    {
        return 0;
    }

    memset(resp, 0, sizeof(*resp));

    if (!sfm_uart_find_header(timeout_ms))
    {
        g_usart2_rx_cnt = 0;
        return 0;
    }

    if (!sfm_uart_read_exact(meta, sizeof(meta), timeout_ms))
    {
        g_usart2_rx_cnt = 0;
        return 0;
    }

    content_len = (uint16_t)(((uint16_t)meta[0] << 8) | meta[1]);
    memcpy(header_plus_len, g_fpm383_header, FPM383_HEADER_LEN);
    header_plus_len[8] = meta[0];
    header_plus_len[9] = meta[1];
    expected_header_chk = fpm383_checksum(header_plus_len, sizeof(header_plus_len));

    if ((content_len < 11U) || (content_len > FPM383_MAX_CONTENT_LEN) || (meta[2] != expected_header_chk))
    {
        g_usart2_rx_cnt = 0;
        return 0;
    }

    if (!sfm_uart_read_exact(content, content_len, timeout_ms))
    {
        g_usart2_rx_cnt = 0;
        return 0;
    }

    expected_content_chk = fpm383_checksum(content, (uint16_t)(content_len - 1U));
    if (content[content_len - 1U] != expected_content_chk)
    {
        g_usart2_rx_cnt = 0;
        return 0;
    }

    resp->cmd1 = content[4];
    resp->cmd2 = content[5];
    resp->status = ((uint32_t)content[6] << 24) |
                   ((uint32_t)content[7] << 16) |
                   ((uint32_t)content[8] << 8) |
                   ((uint32_t)content[9]);

    parsed_data_len = (uint8_t)(content_len - 11U);
    resp->data_len = parsed_data_len;
    if (resp->data_len > sizeof(resp->data))
    {
        resp->data_len = sizeof(resp->data);
    }

    if (resp->data_len > 0U)
    {
        memcpy(resp->data, &content[10], resp->data_len);
    }

    raw_len = (uint16_t)(FPM383_HEADER_LEN + 2U + 1U + content_len);
    if (raw_len > sizeof(g_usart2_rx_buf))
    {
        raw_len = sizeof(g_usart2_rx_buf);
    }

    memcpy((void *)g_usart2_rx_buf, g_fpm383_header, FPM383_HEADER_LEN);
    g_usart2_rx_buf[8] = meta[0];
    g_usart2_rx_buf[9] = meta[1];
    g_usart2_rx_buf[10] = meta[2];
    memcpy((void *)&g_usart2_rx_buf[11], content, raw_len - 11U);
    g_usart2_rx_cnt = raw_len;

    return 1;
}

static int32_t sfm_request(uint8_t cmd1, uint8_t cmd2,
                           const uint8_t *payload, uint16_t payload_len,
                           fpm383_resp_t *resp, uint32_t timeout_ms)
{
    int32_t ack;

    if (!sfm_send_packet(cmd1, cmd2, payload, payload_len))
    {
        return SFM_ACK_FAIL;
    }

    if (!sfm_recv_packet(resp, timeout_ms))
    {
        return SFM_ACK_FAIL;
    }

    ack = sfm_status_to_ack(resp->status);

    if ((resp->cmd1 != cmd1) || (resp->cmd2 != cmd2))
    {
        printf("SFM cmd mismatch tx=%02X %02X rx=%02X %02X st=%08lX ack=%ld\r\n",
               cmd1,
               cmd2,
               resp->cmd1,
               resp->cmd2,
               (unsigned long)resp->status,
               (long)ack);

        // Some 383C firmware variants may not echo request cmd pair strictly.
        if (ack != SFM_ACK_SUCCESS)
        {
            return SFM_ACK_FAIL;
        }
    }

    return ack;
}

void usart2_init(uint32_t baud)
{
    huart2.Init.BaudRate = baud;
    (void)HAL_UART_Init(&huart2);
}

void sfm_touch_init(void)
{
    // PE6 is configured by CubeMX.
}

uint32_t sfm_touch_sta(void)
{
    return (HAL_GPIO_ReadPin(SFM_TOUCH_GPIO_Port, SFM_TOUCH_Pin) == GPIO_PIN_RESET);
}

int32_t sfm_ctrl_led(uint8_t led_start, uint8_t led_end, uint8_t period)
{
    fpm383_resp_t resp;
    uint8_t data[5];
    int32_t rt;

    data[0] = led_start;
    data[1] = led_end;
    data[2] = period;
    data[3] = 0x00;
    data[4] = 0x00;

    memset((void *)g_usart2_rx_buf, 0, sizeof(g_usart2_rx_buf));
    g_usart2_rx_cnt = 0;
    g_usart2_rx_end = 0;

    rt = sfm_request(0x02, 0x0F, data, sizeof(data), &resp, 400);
    if (rt != SFM_ACK_SUCCESS)
    {
        printf("SFM led ack fail, rx_cnt=%lu, err=0x%08lX, rx_state=%lu, b0=%02X b1=%02X b2=%02X b3=%02X\r\n",
               (unsigned long)g_usart2_rx_cnt,
               (unsigned long)huart2.ErrorCode,
               (unsigned long)huart2.RxState,
               g_usart2_rx_buf[0], g_usart2_rx_buf[1], g_usart2_rx_buf[2], g_usart2_rx_buf[3]);
    }

    return rt;
}

int32_t sfm_init(uint32_t baud)
{
    uint32_t baud_try[5] = { baud, 57600U, 115200U, 38400U, 9600U };
    uint8_t i;
    fpm383_resp_t resp;
    int32_t rt;

    sfm_touch_init();

    for (i = 0; i < 5; i++)
    {
        if ((i > 0U) && (baud_try[i] == baud_try[i - 1U]))
        {
            continue;
        }

        usart2_init(baud_try[i]);
        vTaskDelay(pdMS_TO_TICKS(300));

        rt = sfm_request(0x03, 0x03, NULL, 0, &resp, 500);
        if (rt == SFM_ACK_SUCCESS)
        {
            g_sfm_active_baud = baud_try[i];
            g_sfm_state = SFM_STATE_IDLE;
            printf("SFM active baud: %lu\r\n", (unsigned long)g_sfm_active_baud);

            (void)sfm_ctrl_led(0x01, 0x01, 0x05);
            return SFM_ACK_SUCCESS;
        }
    }

    return SFM_ACK_FAIL;
}

int32_t sfm_touch_check(void)
{
    fpm383_resp_t resp;
    int32_t rt;

    // Hardware touch pin is faster and more reliable than polling command response.
    if (sfm_touch_sta() != 0U)
    {
        return SFM_ACK_SUCCESS;
    }

    rt = sfm_request(0x01, 0x35, NULL, 0, &resp, 300);
    if (rt != SFM_ACK_SUCCESS)
    {
        return rt;
    }

    if ((resp.data_len >= 1U) && (resp.data[0] != 0U))
    {
        return SFM_ACK_SUCCESS;
    }

    return SFM_ACK_FAIL;
}

int32_t sfm_wait_touch(uint32_t timeout_ms)
{
    uint32_t start = xTaskGetTickCount();

    while (1)
    {
        if (sfm_touch_check() == SFM_ACK_SUCCESS)
        {
            return SFM_ACK_SUCCESS;
        }

        if (timeout_ms == 0U)
        {
            return SFM_ACK_TIMEOUT;
        }

        if ((xTaskGetTickCount() - start) >= pdMS_TO_TICKS(timeout_ms))
        {
            return SFM_ACK_TIMEOUT;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

int32_t sfm_get_user_total(uint16_t *user_total)
{
    fpm383_resp_t resp;
    int32_t rt;

    if (user_total == NULL)
    {
        return SFM_ACK_FAIL;
    }

    rt = sfm_request(0x02, 0x03, NULL, 0, &resp, 400);
    if (rt != SFM_ACK_SUCCESS)
    {
        return rt;
    }

    if (resp.data_len < 2U)
    {
        return SFM_ACK_FAIL;
    }

    *user_total = (uint16_t)(((uint16_t)resp.data[0] << 8) | resp.data[1]);
    return SFM_ACK_SUCCESS;
}

int32_t sfm_get_unused_id(uint16_t *id)
{
    fpm383_resp_t resp;
    int32_t rt;
    uint16_t capacity;
    uint16_t max_id;
    uint16_t i;

    if (id == NULL)
    {
        return SFM_ACK_FAIL;
    }

    rt = sfm_request(0x01, 0x34, NULL, 0, &resp, 800);
    if (rt != SFM_ACK_SUCCESS)
    {
        return rt;
    }

    if (resp.data_len < 66U)
    {
        return SFM_ACK_FAIL;
    }

    capacity = (uint16_t)(((uint16_t)resp.data[0] << 8) | resp.data[1]);
    max_id = capacity;
    if (max_id > 512U)
    {
        max_id = 512U;
    }

    for (i = 1; i < max_id; i++)
    {
        uint8_t byte_idx = (uint8_t)(i / 8U);
        uint8_t bit_idx = (uint8_t)(i % 8U);
        uint8_t used = (uint8_t)(resp.data[2U + byte_idx] & (1U << bit_idx));
        if (used == 0U)
        {
            *id = i;
            return SFM_ACK_SUCCESS;
        }
    }

    return SFM_ACK_FULL;
}

int32_t sfm_reg_user(uint16_t id)
{
    uint8_t payload[4];
    fpm383_resp_t resp;
    uint32_t start;

    payload[0] = 0x01; // need finger lift between captures
    payload[1] = 0x03; // FPM383C enroll count: 3 captures is the stable baseline
    payload[2] = (uint8_t)(id >> 8);
    payload[3] = (uint8_t)(id & 0xFFU);

    g_sfm_state = SFM_STATE_PROCESSING;

    (void)sfm_ctrl_led(0x01, 0x01, 0x05);

    if (sfm_probe_ready(500) != SFM_ACK_SUCCESS)
    {
        if (sfm_init(SFM_UART_BAUD) != SFM_ACK_SUCCESS)
        {
            g_sfm_state = SFM_STATE_FAILED;
            return SFM_ACK_FAIL;
        }
        (void)sfm_ctrl_led(0x01, 0x01, 0x05);
    }

    if (!sfm_send_packet(0x01, 0x18, payload, sizeof(payload)))
    {
        if ((sfm_init(SFM_UART_BAUD) != SFM_ACK_SUCCESS) || (!sfm_send_packet(0x01, 0x18, payload, sizeof(payload))))
        {
            g_sfm_state = SFM_STATE_FAILED;
            return SFM_ACK_FAIL;
        }
    }

    start = HAL_GetTick();
    while ((HAL_GetTick() - start) < 45000U)
    {
        int32_t ack;

        if (!sfm_recv_packet(&resp, 8000))
        {
            continue;
        }

        if ((resp.cmd1 != 0x01U) || (resp.cmd2 != 0x18U))
        {
            continue;
        }

        ack = sfm_status_to_ack(resp.status);
        if (ack == SFM_ACK_SUCCESS)
        {
            uint8_t progress = (resp.data_len >= 3U) ? resp.data[2] : 100U;
            if (progress >= 100U)
            {
                g_sfm_state = SFM_STATE_SUCCESS;
                return SFM_ACK_SUCCESS;
            }

            continue;
        }

        // Recoverable capture quality/time statuses; keep waiting for next progress frame.
        if ((ack == SFM_ACK_TIMEOUT) || (ack == SFM_ACK_IMAGEERROR) || (ack == SFM_ACK_ALGORITHMFAIL))
        {
            continue;
        }

        g_sfm_state = SFM_STATE_FAILED;
        return ack;
    }

    g_sfm_state = SFM_STATE_FAILED;
    return SFM_ACK_TIMEOUT;
}

int32_t sfm_compare_users(uint16_t *id)
{
    fpm383_resp_t resp;
    int32_t rt;
    uint16_t matched;

    if (id == NULL)
    {
        return SFM_ACK_FAIL;
    }

    rt = sfm_request(0x01, 0x23, NULL, 0, &resp, 1500);
    if (rt != SFM_ACK_SUCCESS)
    {
        return rt;
    }

    if (resp.data_len < 6U)
    {
        return SFM_ACK_FAIL;
    }

    matched = (uint16_t)(((uint16_t)resp.data[0] << 8) | resp.data[1]);
    *id = (uint16_t)(((uint16_t)resp.data[4] << 8) | resp.data[5]);

    // matched!=0 already indicates identification success; keep ID=0 compatible.
    if (matched != 0U)
    {
        return SFM_ACK_SUCCESS;
    }

    return SFM_ACK_NOUSER;
}

int32_t sfm_del_user(uint16_t id)
{
    fpm383_resp_t resp;
    uint8_t payload[3];
    int32_t rt;

    payload[0] = 0x00; // single mode (variant A)
    payload[1] = (uint8_t)(id >> 8);
    payload[2] = (uint8_t)(id & 0xFFU);

    rt = sfm_request(0x01, 0x36, payload, sizeof(payload), &resp, 1200);
    if (rt == SFM_ACK_SUCCESS)
    {
        return rt;
    }

    // Some 383C firmwares use 0x01 as single-delete mode.
    payload[0] = 0x01;
    return sfm_request(0x01, 0x36, payload, sizeof(payload), &resp, 1200);
}

int32_t sfm_del_user_all(void)
{
    fpm383_resp_t resp;
    uint8_t payload[3] = { 0x01, 0xFF, 0xFF }; // all mode (variant A)
    int32_t rt;

    rt = sfm_request(0x01, 0x36, payload, sizeof(payload), &resp, 1800);
    if (rt == SFM_ACK_SUCCESS)
    {
        return rt;
    }

    // Compatible variant on some firmwares.
    payload[0] = 0x00;
    return sfm_request(0x01, 0x36, payload, sizeof(payload), &resp, 1800);
}

const char *sfm_error_code(uint8_t error_code)
{
    const char *p;

    switch(error_code)
    {
        case SFM_ACK_SUCCESS:       p = "OK"; break;
        case SFM_ACK_FAIL:          p = "FAIL"; break;
        case SFM_ACK_FULL:          p = "DB_FULL"; break;
        case SFM_ACK_NOUSER:        p = "NO_USER"; break;
        case SFM_ACK_USER_EXIST:    p = "USER_EXIST"; break;
        case SFM_ACK_TIMEOUT:       p = "TIMEOUT"; break;
        case SFM_ACK_HARDWAREERROR: p = "HW_ERROR"; break;
        case SFM_ACK_IMAGEERROR:    p = "IMG_ERROR"; break;
        case SFM_ACK_BREAK:         p = "BREAK"; break;
        case SFM_ACK_ALGORITHMFAIL: p = "ALGO_FAIL"; break;
        case SFM_ACK_HOMOLOGYFAIL:  p = "HOMOLOGY_FAIL"; break;
        default:                    p = "UNKNOWN_ACK"; break;
    }

    return p;
}

int32_t sfm_verify_fingerprint(uint16_t *user_id, uint32_t timeout)
{
    int32_t rt;
    int32_t last_rt = SFM_ACK_NOUSER;
    uint32_t start_tick;
    uint32_t timeout_ticks;
    uint8_t outer_retry;

    if (user_id == NULL)
    {
        return SFM_ACK_FAIL;
    }

    timeout_ticks = (timeout == 0U) ? pdMS_TO_TICKS(30000U) : pdMS_TO_TICKS(timeout);
    start_tick = xTaskGetTickCount();

    for (outer_retry = 0; outer_retry < 2; outer_retry++)
    {
        (void)sfm_ctrl_led(0x01, 0x01, 0x05);

        if (sfm_probe_ready(400) != SFM_ACK_SUCCESS)
        {
            if (sfm_init(SFM_UART_BAUD) != SFM_ACK_SUCCESS)
            {
                last_rt = SFM_ACK_HARDWAREERROR;
                continue;
            }
            (void)sfm_ctrl_led(0x01, 0x01, 0x05);
        }

        while ((xTaskGetTickCount() - start_tick) < timeout_ticks)
        {
            rt = sfm_compare_users(user_id);
            if (rt == SFM_ACK_SUCCESS)
            {
                return SFM_ACK_SUCCESS;
            }

            last_rt = rt;
            printf("SFM match retry, rt=%s(%ld)\r\n", sfm_error_code((uint8_t)rt), (long)rt);

            if ((rt == SFM_ACK_HARDWAREERROR) || (rt == SFM_ACK_FAIL))
            {
                (void)sfm_init(SFM_UART_BAUD);
                (void)sfm_ctrl_led(0x01, 0x01, 0x05);
                break;
            }

            // Keep scanning in small intervals; no dependency on touch pin.
            vTaskDelay(pdMS_TO_TICKS(120));
        }
    }

    if ((xTaskGetTickCount() - start_tick) >= timeout_ticks)
    {
        return SFM_ACK_TIMEOUT;
    }

    return last_rt;
}

SFM_State_t sfm_get_state(void)
{
    return g_sfm_state;
}

void sfm_reset_state(void)
{
    g_sfm_state = SFM_STATE_IDLE;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    (void)huart;
    // Not used in FPM383C transactional mode.
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        __HAL_UART_CLEAR_OREFLAG(&huart2);
    }
}
