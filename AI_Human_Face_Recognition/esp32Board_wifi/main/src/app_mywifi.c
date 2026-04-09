/* ESPRESSIF MIT License
 * 
 * Copyright (c) 2018 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 * 
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_tls.h"
#include "esp_sntp.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/ip4_addr.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include "app_mywifi.h"
#include "mdns.h"
#include "my_usart.h"

extern uint16_t wifi_Mode; 
//AP模式
char AP_wifi_SSID[30] = "Yahboom_ESP32_WIFI"; //AP-wifi名称
char AP_wifi_PASSWD[20] = ""; //AP-wifi密码

//STA模式
char wifi_SSID[20] = "allgood"; //sta-wifi名称
char wifi_PASSWD[20] = "1145141919810"; //sta-wifi密码


uint8_t sta_ip_connect[4] = {0,0,0,0}; //只有连接上了才会有ip地址打印
#define EXAMPLE_MAX_STA_CONN CONFIG_MAX_STA_CONN

//AP配置
#define EXAMPLE_IP_ADDR CONFIG_SERVER_IP
#define EXAMPLE_ESP_WIFI_AP_CHANNEL CONFIG_ESP_WIFI_AP_CHANNEL
#define EXAMPLE_ESP_MAXIMUM_RETRY CONFIG_ESP_MAXIMUM_RETRY



static const char *TAG = "camera wifi";
static const char *NETWORK_TEST_URL = "http://www.baidu.com";
static const char *VERIFY_GRANT_URL = "http://cloud1-8gfayfnzdb935815-1416338164.ap-shanghai.app.tcloudbase.com/verifyGrant";
#ifndef VERIFY_TLS_PROBE_ON_FAIL
#define VERIFY_TLS_PROBE_ON_FAIL 0
#endif
#ifndef VERIFY_USE_CERT_BUNDLE
#define VERIFY_USE_CERT_BUNDLE 1
#endif
#ifndef VERIFY_HTTP_TIMEOUT_MS
#define VERIFY_HTTP_TIMEOUT_MS 15000 // 增加到 15 秒
#endif
#ifndef VERIFY_HTTP_RETRY_COUNT
#define VERIFY_HTTP_RETRY_COUNT 3 // 增加为 3 次重试
#endif
#ifndef VERIFY_HTTP_RETRY_DELAY_MS
#define VERIFY_HTTP_RETRY_DELAY_MS 1000 // 增加重试间隔到 1 秒以等待网络平复
#endif
#ifndef VERIFY_PROBE_TIMEOUT_MS
#define VERIFY_PROBE_TIMEOUT_MS 4000
#endif
#ifndef VERIFY_DIAG_ONLY_ON_FINAL_FAIL
#define VERIFY_DIAG_ONLY_ON_FINAL_FAIL 1
#endif
/* DigiCert Secure Site OV G2 TLS CN RSA4096 SHA256 2022 CA1 (intermediate CA).
 * Pinning this CA avoids runtime dependency on the global certificate bundle content.
 */
static const char VERIFY_GRANT_CA_CERT_PEM[] =
"-----BEGIN CERTIFICATE-----\n"
"MIIF1TCCBL2gAwIBAgIQBMG5YBkfyr7c2pMBps14wzANBgkqhkiG9w0BAQsFADBh\n"
"MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"
"d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\n"
"MjAeFw0yMjEyMTUwMDAwMDBaFw0zMjEyMTQyMzU5NTlaMGoxCzAJBgNVBAYTAlVT\n"
"MRcwFQYDVQQKEw5EaWdpQ2VydCwgSW5jLjFCMEAGA1UEAxM5RGlnaUNlcnQgU2Vj\n"
"dXJlIFNpdGUgT1YgRzIgVExTIENOIFJTQTQwOTYgU0hBMjU2IDIwMjIgQ0ExMIIC\n"
"IjANBgkqhkiG9w0BAQEFAAOCAg8AMIICCgKCAgEAoqPBWLBJf1bGOdPpKg1poUIy\n"
"2T062PnFJq6TDc4LK8EBlCYpg1XpXjk8tsoSaGw5VIc4K4djyLlAGQXg9VKBbg+7\n"
"5i+8SomphCkVtIPwY+ZwQ0n2puNpVJ6ZgYDYNE9PTmBEYeRPvxpkxsMTivdXXA22\n"
"hAsXZVawlR1/iJXLqmf80UvShmzLXhkR6ymKK0edoJcXDzpgEK3g0cRVCAw8Ij8K\n"
"9nU72msIrhMrMBkAvgOAQEzLGGGWSk92FztzeUuYo4MGCgL0HDEJ0B5hf4V+h/Ct\n"
"Wuho/roYqUUeoHsVMUXPwwGspahGoDlWh9nZ4zRssCE/zXJRYtkplZaUc/5SAOzu\n"
"7F4UatHP1xVfRhkxHTOuCJ7iCyc5B/D1SLpUOFzZ7MfF0eANhB0ilFbFzJ/gXPA9\n"
"dWCYP0kqkkcLcn5hRHQxArGkP+VzZuayTVCbHK5tbZ/ZeQ3gnyEopwtq1Do77dLv\n"
"wZlfnOHA1tzTFYLkATE8VC/xtaSi2tyBoXX+kuvLd/552914gA8+Jw4gM+aYD3y5\n"
"LQCD/6c27beRta2DXC8y4nG1mC2jdyurw/+KxGMKLfuDAeGs9om+kqM5JdJXwUBW\n"
"L13+OQH8VO2omIsoZ2VijBoe6J6r8hzAvg2qz8yIdjG3c+K+clbwmtS0xSx3PHnx\n"
"Ab1dKZPfwGq+TiSx7XcCAwEAAaOCAX4wggF6MBIGA1UdEwEB/wQIMAYBAf8CAQAw\n"
"HQYDVR0OBBYEFCsjFoEbR4mKkHrs6DLUbI5y+c4lMB8GA1UdIwQYMBaAFE4iVCAY\n"
"lebjbuYP+vq5Eu0GF485MA4GA1UdDwEB/wQEAwIBhjAdBgNVHSUEFjAUBggrBgEF\n"
"BQcDAQYIKwYBBQUHAwIwdAYIKwYBBQUHAQEEaDBmMCMGCCsGAQUFBzABhhdodHRw\n"
"Oi8vb2NzcC5kaWdpY2VydC5jbjA/BggrBgEFBQcwAoYzaHR0cDovL2NhY2VydHMu\n"
"ZGlnaWNlcnQuY24vRGlnaUNlcnRHbG9iYWxSb290RzIuY3J0MEAGA1UdHwQ5MDcw\n"
"NaAzoDGGL2h0dHA6Ly9jcmwuZGlnaWNlcnQuY24vRGlnaUNlcnRHbG9iYWxSb290\n"
"RzIuY3JsMD0GA1UdIAQ2MDQwCwYJYIZIAYb9bAIBMAcGBWeBDAEBMAgGBmeBDAEC\n"
"ATAIBgZngQwBAgIwCAYGZ4EMAQIDMA0GCSqGSIb3DQEBCwUAA4IBAQCkniKR2clY\n"
"bEKTe5mh430LVsE62e+yNGFG286R0IW+nKjMEhC99U5ggtp1cjXAZpzsTF4Y7kcT\n"
"g3LsVVnbVWmEDm8yr3YHAg97s/JGnwcJNEg4NqmxAEY+hy7mGIyYVbKSEjWRSxjt\n"
"iiDfaBkwNzFb6fT16xlxEK5LpCpyWS6+2mRWsbNt7C/AaiTvhIizO8zJgiFf3k4S\n"
"SgRFj0nCrCKaakgxpm0EDTG3AV7linTSmQqN1Ca8LsP1umvA6SLk524bdCpT0KM0\n"
"CmAYLruY0BtvgEVLi8JrUWhUqi4lLnDUgqFNDExKmQ/OL60AJZxXKjZyFaceQEGH\n"
"iRipdqqMuIo5\n"
"-----END CERTIFICATE-----\n";
static bool s_sntp_initialized = false;

typedef struct
{
    char *buf;
    size_t cap;
    size_t len;
} http_resp_ctx_t;

static void uart_debug_print(const char *fmt, ...)
{
    char buf[160] = {0};
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len < 0) {
        return;
    }

    if (len >= (int)sizeof(buf)) {
        len = (int)sizeof(buf) - 1;
        buf[len] = '\0';
    }

    Uart_Send_Data((uint8_t *)buf, (uint16_t)len);
}

static void log_verify_heap_snapshot(const char *stage)
{
    uart_debug_print("[NET] heap %s free=%u min=%u largest8=%u largestInt=%u\r\n",
                     (stage != NULL) ? stage : "?",
                     (unsigned)esp_get_free_heap_size(),
                     (unsigned)esp_get_minimum_free_heap_size(),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

static esp_err_t verify_http_event_handler(esp_http_client_event_t *evt)
{
    if ((evt == NULL) || (evt->user_data == NULL))
    {
        return ESP_OK;
    }

    http_resp_ctx_t *ctx = (http_resp_ctx_t *)evt->user_data;
    if ((evt->event_id == HTTP_EVENT_ON_DATA) && (evt->data != NULL) && (evt->data_len > 0))
    {
        size_t left = (ctx->cap > ctx->len) ? (ctx->cap - ctx->len - 1U) : 0U;
        if (left > 0U)
        {
            size_t copy_len = ((size_t)evt->data_len < left) ? (size_t)evt->data_len : left;
            memcpy(ctx->buf + ctx->len, evt->data, copy_len);
            ctx->len += copy_len;
            ctx->buf[ctx->len] = '\0';
        }
    }

    return ESP_OK;
}

static void log_verify_dns_info(const char *url)
{
    if (url == NULL)
    {
        return;
    }

    const char *scheme = strstr(url, "://");
    const char *host_start = (scheme != NULL) ? (scheme + 3) : url;
    const char *host_end = host_start;
    while ((*host_end != '\0') && (*host_end != '/') && (*host_end != ':'))
    {
        host_end++;
    }

    size_t host_len = (size_t)(host_end - host_start);
    if ((host_len == 0U) || (host_len >= 96U))
    {
        uart_debug_print("[NET] verify dns skip: bad host\r\n");
        return;
    }

    char host[96] = {0};
    memcpy(host, host_start, host_len);
    host[host_len] = '\0';

    struct addrinfo hints = {0};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int gai_rs = getaddrinfo(host, NULL, &hints, &res);
    if ((gai_rs != 0) || (res == NULL))
    {
        uart_debug_print("[NET] verify dns fail host=%s gai=%d\r\n", host, gai_rs);
        if (res != NULL)
        {
            freeaddrinfo(res);
        }
        return;
    }

    int idx = 0;
    for (struct addrinfo *it = res; (it != NULL) && (idx < 3); it = it->ai_next)
    {
        if ((it->ai_family == AF_INET) && (it->ai_addr != NULL))
        {
            const struct sockaddr_in *addr4 = (const struct sockaddr_in *)it->ai_addr;
            char ipbuf[16] = {0};
            if (inet_ntoa_r(addr4->sin_addr, ipbuf, sizeof(ipbuf)) != NULL)
            {
                uart_debug_print("[NET] verify dns %s -> %s\r\n", host, ipbuf);
                idx++;
            }
        }
    }

    freeaddrinfo(res);
}

static void log_verify_tcp_probe(const char *url)
{
    if (url == NULL)
    {
        return;
    }

    const char *scheme = strstr(url, "://");
    const char *host_start = (scheme != NULL) ? (scheme + 3) : url;
    const char *host_end = host_start;
    while ((*host_end != '\0') && (*host_end != '/') && (*host_end != ':'))
    {
        host_end++;
    }

    size_t host_len = (size_t)(host_end - host_start);
    if ((host_len == 0U) || (host_len >= 96U))
    {
        uart_debug_print("[NET] verify tcp skip: bad host\r\n");
        return;
    }

    char host[96] = {0};
    memcpy(host, host_start, host_len);
    host[host_len] = '\0';

    struct addrinfo hints = {0};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int gai_rs = getaddrinfo(host, "443", &hints, &res);
    if ((gai_rs != 0) || (res == NULL))
    {
        uart_debug_print("[NET] verify tcp dns fail host=%s gai=%d\r\n", host, gai_rs);
        if (res != NULL)
        {
            freeaddrinfo(res);
        }
        return;
    }

    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0)
    {
        uart_debug_print("[NET] verify tcp socket fail errno=%d\r\n", errno);
        freeaddrinfo(res);
        return;
    }

    struct timeval tv = {
        .tv_sec = 3,
        .tv_usec = 0,
    };
    (void)setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    (void)setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    int conn_rs = connect(sock, res->ai_addr, (socklen_t)res->ai_addrlen);
    if (conn_rs == 0)
    {
        const struct sockaddr_in *addr4 = (const struct sockaddr_in *)res->ai_addr;
        char ipbuf[16] = {0};
        (void)inet_ntoa_r(addr4->sin_addr, ipbuf, sizeof(ipbuf));
        uart_debug_print("[NET] verify tcp ok %s:443\r\n", ipbuf);
    }
    else
    {
        uart_debug_print("[NET] verify tcp fail errno=%d\r\n", errno);
    }

    close(sock);
    freeaddrinfo(res);
}

static void log_verify_tls_probe(const char *url)
{
    if (url == NULL)
    {
        return;
    }

    const char *scheme = strstr(url, "://");
    const char *host_start = (scheme != NULL) ? (scheme + 3) : url;
    const char *host_end = host_start;
    while ((*host_end != '\0') && (*host_end != '/') && (*host_end != ':'))
    {
        host_end++;
    }

    size_t host_len = (size_t)(host_end - host_start);
    if ((host_len == 0U) || (host_len >= 96U))
    {
        uart_debug_print("[NET] verify tls probe skip: bad host\r\n");
        return;
    }

    char host[96] = {0};
    memcpy(host, host_start, host_len);
    host[host_len] = '\0';

    esp_tls_t *tls = esp_tls_init();
    if (tls == NULL)
    {
        uart_debug_print("[NET] verify tls probe init fail\r\n");
        return;
    }

    esp_tls_cfg_t cfg = {
        .cacert_buf = VERIFY_USE_CERT_BUNDLE ? NULL : (const unsigned char *)VERIFY_GRANT_CA_CERT_PEM,
        .cacert_bytes = VERIFY_USE_CERT_BUNDLE ? 0 : sizeof(VERIFY_GRANT_CA_CERT_PEM),
        .crt_bundle_attach = VERIFY_USE_CERT_BUNDLE ? esp_crt_bundle_attach : NULL,
        .common_name = host,
        .timeout_ms = VERIFY_PROBE_TIMEOUT_MS,
        .tls_version = ESP_TLS_VER_TLS_1_2,
    };

    int rs = esp_tls_conn_new_sync(host, (int)strlen(host), 443, &cfg, tls);
    if (rs > 0)
    {
        uart_debug_print("[NET] verify tls probe ok\r\n");
        (void)esp_tls_conn_destroy(tls);
        return;
    }

    esp_tls_error_handle_t err_h = NULL;
    int tls_last_code = 0;
    int tls_flags = 0;
    int mbed_err = 0;
    int sys_err = 0;
    int esp_err = 0;

    if (esp_tls_get_error_handle(tls, &err_h) == ESP_OK)
    {
        (void)esp_tls_get_and_clear_last_error(err_h, &tls_last_code, &tls_flags);
        (void)esp_tls_get_and_clear_error_type(err_h, ESP_TLS_ERR_TYPE_MBEDTLS, &mbed_err);
        (void)esp_tls_get_and_clear_error_type(err_h, ESP_TLS_ERR_TYPE_SYSTEM, &sys_err);
        (void)esp_tls_get_and_clear_error_type(err_h, ESP_TLS_ERR_TYPE_ESP, &esp_err);
    }

    uart_debug_print("[NET] verify tls probe fail rs=%d tls_last=0x%x flags=0x%x mbed=0x%x sys=%d esp=0x%x\r\n",
                     rs,
                     tls_last_code,
                     tls_flags,
                     mbed_err,
                     sys_err,
                     esp_err);

    (void)esp_tls_conn_destroy(tls);
}

static int s_retry_num = 0;
/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group = NULL;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static bool ensure_time_synced(void)
{
    time_t now = 0;
    struct tm timeinfo = {0};

    time(&now);
    localtime_r(&now, &timeinfo);
    if ((timeinfo.tm_year + 1900) >= 2024)
    {
        return true;
    }

    if (!s_sntp_initialized)
    {
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_setservername(0, "ntp.aliyun.com");
        sntp_setservername(1, "pool.ntp.org");
        sntp_init();
        s_sntp_initialized = true;
        uart_debug_print("[NET] sntp init\r\n");
    }

    for (int i = 0; i < 15; ++i)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        time(&now);
        localtime_r(&now, &timeinfo);
        if ((timeinfo.tm_year + 1900) >= 2024)
        {
            uart_debug_print("[NET] time synced: %04d-%02d-%02d\r\n",
                             timeinfo.tm_year + 1900,
                             timeinfo.tm_mon + 1,
                             timeinfo.tm_mday);
            return true;
        }
    }

    uart_debug_print("[NET] time sync timeout\r\n");
    return false;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    switch(event_id) {
        case WIFI_EVENT_AP_STACONNECTED:
            wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
            ESP_LOGI(TAG, "station:" MACSTR " join, AID=%d", MAC2STR(event->mac), event->aid);
            break;

        case WIFI_EVENT_AP_STADISCONNECTED:
            wifi_event_ap_stadisconnected_t *ds_event = (wifi_event_ap_stadisconnected_t *)event_data;
            ESP_LOGI(TAG, "station:" MACSTR "leave, AID=%d", MAC2STR(ds_event->mac), ds_event->aid);
            break;

        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
            uart_debug_print("[WIFI] STA start\r\n");
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY)
            {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI(TAG, "retry to connect to the AP");
                uart_debug_print("[WIFI] retry connect %d\r\n", s_retry_num);
            }
            else {
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                uart_debug_print("[WIFI] connect failed\r\n");
            }
            ESP_LOGI(TAG, "connect to the AP fail");
            break;
        default:
            break;
    }
    // mdns_handle_system_event(ctx, event);
    return;
}


static void ip_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    ip_event_got_ip_t *event;

    switch(event_id) {
        case IP_EVENT_STA_GOT_IP:
            event = (ip_event_got_ip_t*)event_data;
            ESP_LOGI(TAG, "got ip:" IPSTR "\n", IP2STR(&event->ip_info.ip));
            uart_debug_print("[WIFI] got ip: " IPSTR "\r\n", IP2STR(&event->ip_info.ip));
            //保存ip
            sta_ip_connect[0] = esp_ip4_addr_get_byte(&event->ip_info.ip, 0);
            sta_ip_connect[1] = esp_ip4_addr_get_byte(&event->ip_info.ip, 1);
            sta_ip_connect[2] = esp_ip4_addr_get_byte(&event->ip_info.ip, 2);
            sta_ip_connect[3] = esp_ip4_addr_get_byte(&event->ip_info.ip, 3);

            s_retry_num = 0;
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            break;
        default:
            break;
    }
    return;
}

void wifi_init_softap(esp_netif_t * netif)
{
    if (strcmp(EXAMPLE_IP_ADDR, "192.168.4.1"))
    {
        int a, b, c, d;
        sscanf(EXAMPLE_IP_ADDR, "%d.%d.%d.%d", &a, &b, &c, &d);
        esp_netif_ip_info_t ip_info;
        IP4_ADDR(&ip_info.ip, a, b, c, d);
        IP4_ADDR(&ip_info.gw, a, b, c, d);
        IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
        ESP_ERROR_CHECK(esp_netif_dhcps_stop(netif));
        ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ip_info));
        ESP_ERROR_CHECK(esp_netif_dhcps_start(netif));
    }
    wifi_config_t wifi_config = {
        // .ap.ssid = EXAMPLE_ESP_WIFI_AP_SSID,
        // .ap.password = EXAMPLE_ESP_WIFI_AP_PASS,
        .ap.ssid_len = strlen((char *)wifi_config.ap.ssid),
        .ap.channel = 1,
        .ap.authmode = WIFI_AUTH_WPA2_PSK,
        // .ap.max_connection = EXAMPLE_MAX_STA_CONN,
        .ap.max_connection = 4,
        .ap.beacon_interval = 100,
    };
    sprintf((char *)wifi_config.sta.ssid,"%s",(char *)AP_wifi_SSID);
    sprintf((char *)wifi_config.sta.password,"%s",(char *)AP_wifi_PASSWD);

    if (strlen(AP_wifi_PASSWD) == 0)
    {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }
    if (strlen(EXAMPLE_ESP_WIFI_AP_CHANNEL))
    {
        int channel;
        sscanf(EXAMPLE_ESP_WIFI_AP_CHANNEL, "%d", &channel);
        wifi_config.ap.channel = channel;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    ESP_LOGI(TAG, "wifi_init_softap finished.SSID:%s password:%s",
             AP_wifi_SSID, AP_wifi_PASSWD);
}


void wifi_init_sta()
{
    wifi_config_t wifi_config = {0};
    sprintf((char *)wifi_config.sta.ssid,"%s",(char *)wifi_SSID);
    sprintf((char *)wifi_config.sta.password,"%s",(char *)wifi_PASSWD);


    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    ESP_LOGI(TAG, "wifi_init_sta finished.");
    ESP_LOGI(TAG, "connect to ap SSID:%s password:%s",
             wifi_SSID, wifi_PASSWD);
}

void app_mywifi_main()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_mode_t mode = WIFI_MODE_STA;

    mode = WIFI_MODE_APSTA;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(mode));

    if (mode & WIFI_MODE_AP)
    {
        esp_netif_t * ap_netif = esp_netif_create_default_wifi_ap();
        wifi_init_softap(ap_netif);
    }

    if (mode & WIFI_MODE_STA)
    {
        esp_netif_create_default_wifi_sta();
        wifi_init_sta();
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_LOGI(TAG, "wifi init finished.");
    uart_debug_print("[WIFI] init finished\r\n");

    if (mode & WIFI_MODE_STA) {
        xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);
    }
    vEventGroupDelete(s_wifi_event_group);
    s_wifi_event_group = NULL;
}

void app_mywifi_network_test()
{
    uart_debug_print("[NET] test start: %s\r\n", NETWORK_TEST_URL);

    esp_http_client_config_t config = {
        .url = NETWORK_TEST_URL,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL)
    {
        ESP_LOGE(TAG, "network test init failed");
        uart_debug_print("[NET] init failed\r\n");
        return;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        int status = esp_http_client_get_status_code(client);
        int length = esp_http_client_get_content_length(client);
        ESP_LOGI(TAG, "network test ok, status=%d, content_length=%d", status, length);
        uart_debug_print("[NET] ok status=%d len=%d\r\n", status, length);
    }
    else
    {
        ESP_LOGE(TAG, "network test failed: %s", esp_err_to_name(err));
        uart_debug_print("[NET] failed: %s\r\n", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

app_verify_result_t app_mywifi_verify_grant_ex(const char *grant_token, const char *lock_id)
{
    if ((grant_token == NULL) || (grant_token[0] == '\0'))
    {
        uart_debug_print("[NET] verify skip: empty token\r\n");
        return APP_VERIFY_RESULT_REJECT;
    }

    if ((VERIFY_GRANT_URL == NULL) || (strstr(VERIFY_GRANT_URL, "your-server") != NULL))
    {
        uart_debug_print("[NET] verify url not configured\r\n");
        return APP_VERIFY_RESULT_NET_ERR;
    }

    char body[256] = {0};
    char resp[512] = {0};
    http_resp_ctx_t resp_ctx = {
        .buf = resp,
        .cap = sizeof(resp),
        .len = 0,
    };
    snprintf(body, sizeof(body), "{\"grantToken\":\"%s\",\"lockId\":\"%s\"}",
             grant_token,
             (lock_id != NULL) ? lock_id : "");

    (void)ensure_time_synced();

    log_verify_dns_info(VERIFY_GRANT_URL);

    esp_http_client_config_t config = {
        .url = VERIFY_GRANT_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = VERIFY_HTTP_TIMEOUT_MS,
#if defined(HTTP_ADDR_TYPE_INET)
        .addr_type = HTTP_ADDR_TYPE_INET,
#endif
        .event_handler = verify_http_event_handler,
        .user_data = &resp_ctx,
    };

    esp_err_t err = ESP_FAIL;
    int status = 0;
    for (int attempt = 0; attempt < VERIFY_HTTP_RETRY_COUNT; ++attempt)
    {
        bool need_diag =
            (VERIFY_DIAG_ONLY_ON_FINAL_FAIL == 0) ||
            (attempt == (VERIFY_HTTP_RETRY_COUNT - 1));

        resp_ctx.len = 0;
        resp[0] = '\0';

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == NULL)
        {
            uart_debug_print("[NET] verify init failed\r\n");
            return APP_VERIFY_RESULT_NET_ERR;
        }

        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_header(client, "Connection", "close");
        esp_http_client_set_post_field(client, body, (int)strlen(body));

        log_verify_heap_snapshot("before verify");

        err = esp_http_client_perform(client);
        if (err == ESP_OK)
        {
            status = esp_http_client_get_status_code(client);
            esp_http_client_cleanup(client);
            break;
        }

        int sock_errno = esp_http_client_get_errno(client);
        esp_http_client_transport_t tp = esp_http_client_get_transport_type(client);

        uart_debug_print("[NET] verify req failed(a%d): %s errno=%d tp=%d\r\n",
                         attempt + 1,
                         esp_err_to_name(err),
                         sock_errno,
                         (int)tp);
        log_verify_heap_snapshot("on verify fail");
        if (need_diag)
        {
            log_verify_tcp_probe(VERIFY_GRANT_URL);
        #if VERIFY_TLS_PROBE_ON_FAIL
            log_verify_tls_probe(VERIFY_GRANT_URL);
        #endif
        }

        esp_http_client_cleanup(client);

        if (attempt < (VERIFY_HTTP_RETRY_COUNT - 1))
        {
            uart_debug_print("[NET] verify retry\r\n");
            vTaskDelay(pdMS_TO_TICKS(VERIFY_HTTP_RETRY_DELAY_MS));
        }
    }

    if (err != ESP_OK)
    {
        return APP_VERIFY_RESULT_NET_ERR;
    }

    bool valid = false;
    if ((status >= 200) && (status < 300))
    {
        if ((strstr(resp, "\"valid\":true") != NULL) ||
            (strstr(resp, "\"valid\": true") != NULL) ||
            (strstr(resp, "\"success\":true") != NULL))
        {
            valid = true;
        }
    }

    uart_debug_print("[NET] verify resp=%s\r\n", resp);
    uart_debug_print("[NET] verify status=%d valid=%d\r\n", status, valid ? 1 : 0);
    return valid ? APP_VERIFY_RESULT_OK : APP_VERIFY_RESULT_REJECT;
}

bool app_mywifi_verify_grant(const char *grant_token, const char *lock_id)
{
    return (app_mywifi_verify_grant_ex(grant_token, lock_id) == APP_VERIFY_RESULT_OK);
}