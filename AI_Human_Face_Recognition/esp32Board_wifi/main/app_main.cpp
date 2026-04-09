#include "yahboom_camera.h"
#include "app_mywifi.h"
#include "app_myhttpd.hpp"
#include "app_mymdns.h"

#include "esp_log.h"
#include "driver/spi_common.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "esp_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "my_usart.h"
#include "my_usart1_user.h"
#include "my_ble_unlock.h"
#include "my_user_iic.h"
#include "mykey.h"
#include <cstring>

//AI识别部分
#include "yahboom_human_face_recognition.hpp" //人脸识别

//可以在配置改成串口，目前是串口

static QueueHandle_t xQueueAIFrame = NULL;
static QueueHandle_t xQueueAIUSERFrame = NULL;
static QueueHandle_t xQueuemyvirtualKey = NULL;

#define UART1_DIAG_ENABLE 0

static const char TAG[] = "main_AI_version";
char Version[] = "AI_V1.5.1";
uint16_t wifi_Mode = 2; 

static void uart1_diag_task(void *arg)
{
    (void)arg;
    static const uint8_t ping_cmd[] = "PING\r\n";
    static const char ping_log[] = "[LINK TEST] UART1 -> STM32: PING\r\n";
    while (1)
    {
        Uart1_Send_Data((uint8_t *)ping_cmd, (uint16_t)(sizeof(ping_cmd) - 1U));
        Uart_Send_Data((uint8_t *)ping_log, (uint16_t)strlen(ping_log));
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

extern "C" void app_main(void)
{
    uint8_t ver_data[50]="\0";

    xQueuemyvirtualKey = xQueueCreate(4, sizeof(myrecognizer_state_t));
    if (xQueuemyvirtualKey == NULL)
    {
        ESP_LOGE(TAG, "virtual key queue alloc fail");
        return;
    }

    // 先初始化USB调试串口，确保后续摄像头/识别初始化日志可见。
    My_Uart_Init(xQueuemyvirtualKey);

    xQueueAIFrame = xQueueCreate(2, sizeof(camera_fb_t *));

    xQueueAIUSERFrame = xQueueCreate(2, sizeof(camera_fb_t *));

    if ((xQueueAIFrame == NULL) || (xQueueAIUSERFrame == NULL))
    {
        Uart_Send_Data((uint8_t *)"[BOOT] ai queue alloc fail, direct mode\r\n", 40);
    }
    else
    {
        Uart_Send_Data((uint8_t *)"[BOOT] queues ok\r\n", 18);
    }

    My_Uart1_user_Init(xQueuemyvirtualKey);//串口1初始化
    My_i2c_init(xQueuemyvirtualKey);//I2C初始化 

    //连接wifi
    app_mywifi_main();
    app_mywifi_network_test();

    // BLE uses NimBLE controller and relies on initialized NVS state.
    Uart_Send_Data((uint8_t *)"[BOOT] call BLE init\r\n", 22);
    My_BLE_Unlock_Init();
    Uart_Send_Data((uint8_t *)"[BOOT] BLE init returned\r\n", 26);

    //摄像头相关
    my_register_camera(PIXFORMAT_RGB565, FRAMESIZE_QVGA, 2, xQueueAIFrame);// FRAMESIZE_VGA,会卡， _240X240
    Uart_Send_Data((uint8_t *)"[BOOT] camera reg done\r\n", 24);
    app_mymdns_main();

    // 人脸识别（直拉摄像头模式，不依赖中间帧队列）
    register_human_face_recognition(NULL, xQueuemyvirtualKey, NULL, NULL, true);
    Uart_Send_Data((uint8_t *)"[BOOT] face reg done\r\n", 22);
    // 先关闭HTTP视频流，避免占用帧资源，优先保证人脸开锁链路稳定。
    // register_httpd(xQueueAIUSERFrame, NULL, true);
    Uart_Send_Data((uint8_t *)"[BOOT] httpd off\r\n", 18);

    //按键初始化 
    Key_Init(xQueuemyvirtualKey);

    //直接把版本号发送出来+ 串口助手也能查询
    sprintf((char*)ver_data,"YAHBOOM Board Ver:%s\r\n",Version);
    Uart_Send_Data(ver_data,strlen((char*)ver_data)); 
    Uart_Send_Data((uint8_t *)"[FACE] mode=idle wait cmd\r\n", 27);

    #if UART1_DIAG_ENABLE
    // 上电链路自检：给STM32发送一次锁门命令，便于确认UART1<->USART3已连通。
    Uart1_Send_Lock();
    Uart_Send_Data((uint8_t *)"[LINK TEST] UART1 -> STM32: LOCK\r\n", 35);

    xTaskCreate(uart1_diag_task, "uart1_diag_task", 3 * 1024, NULL, 4, NULL);
    #endif

}
