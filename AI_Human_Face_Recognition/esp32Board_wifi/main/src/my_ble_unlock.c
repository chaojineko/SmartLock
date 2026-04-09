#include "my_ble_unlock.h"

#include "my_usart.h"
#include "my_usart1_user.h"

#include <string.h>
#include <ctype.h>

#if defined(CONFIG_BT_ENABLED) && CONFIG_BT_ENABLED && defined(CONFIG_BT_NIMBLE_ENABLED) && CONFIG_BT_NIMBLE_ENABLED

#include "esp_log.h"
#include "esp_err.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *BLETAG = "BLE_UNLOCK";

static uint8_t g_ble_addr_type;

// UUID: 6f0af5f2-8d42-4f24-b5f2-9af53dd3a001
static const ble_uuid128_t g_lock_service_uuid =
    BLE_UUID128_INIT(0x01, 0xa0, 0xd3, 0x3d, 0xf5, 0x9a, 0xf2, 0xb5,
                     0x24, 0x4f, 0x42, 0x8d, 0xf2, 0xf5, 0x0a, 0x6f);

// UUID: 6f0af5f2-8d42-4f24-b5f2-9af53dd3a002
static const ble_uuid128_t g_lock_char_uuid =
    BLE_UUID128_INIT(0x02, 0xa0, 0xd3, 0x3d, 0xf5, 0x9a, 0xf2, 0xb5,
                     0x24, 0x4f, 0x42, 0x8d, 0xf2, 0xf5, 0x0a, 0x6f);

static int ble_lock_gap_event(struct ble_gap_event *event, void *arg);

static void ble_lock_normalize_cmd(const char *src, size_t len, char *dst, size_t dst_sz)
{
    size_t i = 0;
    size_t j = 0;

    if ((src == NULL) || (dst == NULL) || (dst_sz == 0U))
    {
        return;
    }

    while ((i < len) && (j + 1U < dst_sz))
    {
        char ch = src[i++];
        if ((ch == '\r') || (ch == '\n') || (ch == ' ') || (ch == '\t'))
        {
            continue;
        }
        dst[j++] = (char)tolower((unsigned char)ch);
    }
    dst[j] = '\0';
}

static int ble_lock_gatt_access(uint16_t conn_handle,
                                uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt,
                                void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if ((ctxt == NULL) || (ctxt->om == NULL))
    {
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
    {
        char raw[64] = {0};
        char cmd[64] = {0};
        uint16_t raw_len = OS_MBUF_PKTLEN(ctxt->om);
        uint16_t copy_len = (raw_len < (uint16_t)(sizeof(raw) - 1U)) ? raw_len : (uint16_t)(sizeof(raw) - 1U);

        int rc = ble_hs_mbuf_to_flat(ctxt->om, raw, copy_len, NULL);
        if (rc != 0)
        {
            return BLE_ATT_ERR_UNLIKELY;
        }

        raw[copy_len] = '\0';
        ble_lock_normalize_cmd(raw, copy_len, cmd, sizeof(cmd));

        if ((strcmp(cmd, "unlock") == 0) || (strcmp(cmd, "u") == 0) || (strcmp(cmd, "ble_unlock") == 0))
        {
            Uart1_Send_Unlock();
            Uart_Send_Data((uint8_t *)"[BLE] unlock cmd\r\n", 18);
            return 0;
        }

        if ((strcmp(cmd, "lock") == 0) || (strcmp(cmd, "l") == 0) || (strcmp(cmd, "ble_lock") == 0))
        {
            Uart1_Send_Lock();
            Uart_Send_Data((uint8_t *)"[BLE] lock cmd\r\n", 16);
            return 0;
        }

        Uart_Send_Data((uint8_t *)"[BLE] unknown cmd\r\n", 19);
        return 0;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        const char *text = "LOCK_CTRL";
        int rc = os_mbuf_append(ctxt->om, text, strlen(text));
        return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def g_ble_gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &g_lock_service_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &g_lock_char_uuid.u,
                .access_cb = ble_lock_gatt_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                0,
            },
        },
    },
    {
        0,
    },
};

static void ble_lock_advertise(void)
{
    struct ble_hs_adv_fields fields;
    struct ble_gap_adv_params adv_params;
    int rc;

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (const uint8_t *)"ESP32_LOCK";
    fields.name_len = (uint8_t)strlen("ESP32_LOCK");
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
        char msg[64] = {0};
        int n = snprintf(msg, sizeof(msg), "[BLE] adv fields fail:%d\r\n", rc);
        if (n > 0)
        {
            Uart_Send_Data((uint8_t *)msg, (uint16_t)n);
        }
        ESP_LOGE(BLETAG, "adv fields failed: %d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(g_ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params, ble_lock_gap_event, NULL);
    if (rc != 0)
    {
        char msg[64] = {0};
        int n = snprintf(msg, sizeof(msg), "[BLE] adv start fail:%d\r\n", rc);
        if (n > 0)
        {
            Uart_Send_Data((uint8_t *)msg, (uint16_t)n);
        }
        ESP_LOGE(BLETAG, "adv start failed: %d", rc);
    }
    else
    {
        Uart_Send_Data((uint8_t *)"[BLE] advertising ESP32_LOCK\r\n", 29);
    }
}

static int ble_lock_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    if (event == NULL)
    {
        return 0;
    }

    switch (event->type)
    {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0)
            {
                Uart_Send_Data((uint8_t *)"[BLE] connected\r\n", 17);
            }
            else
            {
                ble_lock_advertise();
            }
            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            Uart_Send_Data((uint8_t *)"[BLE] disconnected\r\n", 20);
            ble_lock_advertise();
            return 0;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            ble_lock_advertise();
            return 0;

        default:
            return 0;
    }
}

static void ble_lock_on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &g_ble_addr_type);
    if (rc != 0)
    {
        char msg[64] = {0};
        int n = snprintf(msg, sizeof(msg), "[BLE] sync fail:%d\r\n", rc);
        if (n > 0)
        {
            Uart_Send_Data((uint8_t *)msg, (uint16_t)n);
        }
        ESP_LOGE(BLETAG, "ble_hs_id_infer_auto failed: %d", rc);
        return;
    }

    Uart_Send_Data((uint8_t *)"[BLE] sync ok\r\n", 15);
    ble_lock_advertise();
}

static void ble_lock_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void My_BLE_Unlock_Init(void)
{
    int rc;

    rc = nimble_port_init();
    if (rc != 0)
    {
        char msg[96] = {0};
        int n = snprintf(msg, sizeof(msg), "[BLE] nimble init failed:%d %s\r\n", rc, esp_err_to_name(rc));
        if (n > 0)
        {
            Uart_Send_Data((uint8_t *)msg, (uint16_t)n);
        }
        ESP_LOGE(BLETAG, "nimble_port_init failed: %d", rc);
        return;
    }

    ble_hs_cfg.sync_cb = ble_lock_on_sync;
    ble_hs_cfg.gatts_register_cb = NULL;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("ESP32_LOCK");

    rc = ble_gatts_count_cfg(g_ble_gatt_svr_svcs);
    if (rc == 0)
    {
        rc = ble_gatts_add_svcs(g_ble_gatt_svr_svcs);
    }

    if (rc != 0)
    {
        ESP_LOGE(BLETAG, "gatt init failed: %d", rc);
        Uart_Send_Data((uint8_t *)"[BLE] gatt failed\r\n", 19);
        return;
    }

    Uart_Send_Data((uint8_t *)"[BLE] init ok\r\n", 15);
    nimble_port_freertos_init(ble_lock_host_task);
}

#else

void My_BLE_Unlock_Init(void)
{
    Uart_Send_Data((uint8_t *)"[BLE] disabled in sdkconfig\r\n", 29);
}

#endif
