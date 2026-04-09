#include "stdio.h"
#include "my_usart.h"
#include "my_usart1_user.h"
#include "string.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include <ctype.h>

static const char *UARTTAG = "UART1_USER";

const int uart1_buffer_size = (1024 * 2);

extern uint8_t sta_ip_connect[4]; //这个是sta_ip相关的
extern uint16_t wifi_Mode;    //wifi模式 0:AP  1:sta  2:AP+STA

static QueueHandle_t xQueuevirtualKeystate = NULL; //虚拟按键
static myrecognizer_state_t recognizer_state = myIDLE;//默认待机，不持续识别
static volatile uint32_t g_uart1_ack_unlock_tick = 0U;
static volatile uint32_t g_uart1_ack_lock_tick = 0U;
static volatile uint32_t g_uart1_ack_lock_hold_tick = 0U;

uint32_t Uart1_Get_AckUnlockTick(void)
{
    return g_uart1_ack_unlock_tick;
}

bool Uart1_Wait_AckUnlock_After(uint32_t prev_tick, uint32_t timeout_ms)
{
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    if (timeout_ticks == 0U)
    {
        timeout_ticks = 1U;
    }

    for (;;)
    {
        uint32_t cur = g_uart1_ack_unlock_tick;
        if ((cur != 0U) && (cur != prev_tick))
        {
            return true;
        }

        if ((xTaskGetTickCount() - start) >= timeout_ticks)
        {
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void uart1_normalize_cmd(const uint8_t *buff, uint16_t len, char *out, size_t out_sz)
{
    size_t copy_len;
    size_t i;

    if ((out == NULL) || (out_sz == 0U))
    {
        return;
    }

    out[0] = '\0';
    if ((buff == NULL) || (len == 0U))
    {
        return;
    }

    copy_len = (len < (uint16_t)(out_sz - 1U)) ? (size_t)len : (out_sz - 1U);
    memcpy(out, buff, copy_len);
    out[copy_len] = '\0';

    while ((copy_len > 0U) && ((out[copy_len - 1U] == '\r') || (out[copy_len - 1U] == '\n') || (out[copy_len - 1U] == ' ') || (out[copy_len - 1U] == '\t')))
    {
        out[copy_len - 1U] = '\0';
        copy_len--;
    }

    for (i = 0; i < copy_len; ++i)
    {
        out[i] = (char)tolower((unsigned char)out[i]);
    }
}

// 串口接收和发送任务 ph2.0的接收任务
static void My_Uart1_user_Task(void *arg)
{
    //ESP_LOGI(TAG, "Start Uart0_Rx_Task with core:%d", xPortGetCoreID());
    uint16_t temp_len = 255;
    uint8_t* temp_data = (uint8_t*) malloc(temp_len);
    

    while (1) 
    {
        // 从串口0读取数据 
        const int rxBytes = uart_read_bytes(UART1_MUN, temp_data, temp_len, 1);  // /portTICK_PERIOD_MS
        if (rxBytes > 0)
        {
              if (rxBytes < temp_len)
              {
                    temp_data[rxBytes] = '\0';
              }
           //Uart1_Send_Data(temp_data,rxBytes);//直接发送(回显)
           Deal_uart1_massage(temp_data,rxBytes);
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
    free(temp_data);
    vTaskDelete(NULL);
}



// 通过串口发送一串数据 
int Uart1_Send_Data(uint8_t* data, uint16_t len)
{
    const int txBytes = uart_write_bytes(UART1_MUN, data, len);
    return txBytes;
}

// 通过串口发送一个字节 
int Uart1_Send_Byte(uint8_t data)
{
    uint8_t data1 = data;
    const int txBytes = uart_write_bytes(UART1_MUN, &data1, 1);
    return txBytes;
}

void Uart1_Send_Unlock(void)
{
    // Keep lock-control frame short and robust on shared UART line.
    static const uint8_t unlock_text[] = "U\r\n";
    Uart1_Send_Data((uint8_t *)unlock_text, (uint16_t)(sizeof(unlock_text) - 1U));
    (void)uart_wait_tx_done(UART1_MUN, pdMS_TO_TICKS(30));
    Uart_Send_Data((uint8_t *)"[UART1 TX] UNLOCK\r\n", 19);
}

void Uart1_Send_Lock(void)
{
    static const uint8_t lock_text[] = "L\r\n";
    Uart1_Send_Data((uint8_t *)lock_text, (uint16_t)(sizeof(lock_text) - 1U));
    (void)uart_wait_tx_done(UART1_MUN, pdMS_TO_TICKS(30));
    Uart_Send_Data((uint8_t *)"[UART1 TX] LOCK\r\n", 17);
}


// 初始化串口, 波特率为115200 
//后期可能要加模式选择标志按键 -尽可能的合在一起
void My_Uart1_user_Init(const QueueHandle_t key_state_i)
{
    //引入虚拟按键 virtual
     xQueuevirtualKeystate = key_state_i; 


    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        //.source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(UART1_MUN, uart1_buffer_size, uart1_buffer_size, 0,NULL, 0);
    uart_param_config(UART1_MUN, &uart_config);
    uart_set_pin(UART1_MUN, UART1_GPIO_TXD, UART1_GPIO_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    Uart_Send_Data((uint8_t *)"[UART1] 115200 ready\r\n", 22);
    
    xTaskCreate(My_Uart1_user_Task, "My_Uart1_user_Task", 5*1024, NULL, 5, NULL);
}



static uint8_t SetRECV[40]="\0";
void Deal_uart1_massage(uint8_t *buff,uint16_t len)//处理串口接收到的数据
{
    char cmd[64] = {0};

    uart1_normalize_cmd(buff, len, cmd, sizeof(cmd));
    if (cmd[0] == '\0')
    {
        return;
    }

    if ((strncmp(cmd, "ack:", 4) == 0) || (strncmp(cmd, "err:", 4) == 0))
    {
        uint32_t now_tick = (uint32_t)xTaskGetTickCount();

        if (strcmp(cmd, "ack:unlock") == 0)
        {
            g_uart1_ack_unlock_tick = now_tick;
        }
        else if ((strcmp(cmd, "ack:lock") == 0) || (strcmp(cmd, "ack:autolock") == 0))
        {
            g_uart1_ack_lock_tick = now_tick;
        }
        else if (strcmp(cmd, "ack:lock:hold") == 0)
        {
            g_uart1_ack_lock_hold_tick = now_tick;
        }

        char msg[80] = {0};
        snprintf(msg, sizeof(msg), "[STM32] %s\r\n", cmd);
        Uart_Send_Data((uint8_t *)msg, (uint16_t)strlen(msg));
        return;
    }
    
    if (strcmp(cmd, "sta_ip") == 0)
    {
        if (sta_ip_connect[0] == 0)//说明没有ip
        {
            sprintf((char*)SetRECV,"sta_ip:null\r\n");
            Uart_Send_Data(SetRECV,strlen((char*)SetRECV));
        }
        else
        {
            sprintf((char*)SetRECV,"sta_ip:%d.%d.%d.%d\r\n",sta_ip_connect[0],sta_ip_connect[1],sta_ip_connect[2],sta_ip_connect[3]);
            Uart_Send_Data(SetRECV,strlen((char*)SetRECV));
        }   
    }


    else if  (strcmp(cmd, "ap_ip") == 0)
    {
        if (wifi_Mode == 1)//说明没有ip 只开sta模式
        {
            sprintf((char*)SetRECV,"ap_ip:null\r\n");
            Uart_Send_Data(SetRECV,strlen((char*)SetRECV));
        }
        else
        {
            sprintf((char*)SetRECV,"ap_ip:192.168.4.1\r\n"); //固定就好
            Uart_Send_Data(SetRECV,strlen((char*)SetRECV));
        }   
    }

    else //虚拟按键选择
    {
        uart1_Get_Key_massage((uint8_t *)cmd, (uint16_t)strlen(cmd));
    }
    memset(SetRECV,0,sizeof(SetRECV));//清空它

}

extern bool register_mode;//颜色识别的注册标志 和串口0同步


void uart1_Get_Key_massage(uint8_t *buff,uint16_t len)
{
    if (strcmp((char *)buff, "key_menu") == 0)//eg:KEY_MENU
    {
        // //人脸识别
       
        recognizer_state = myENROLL;//登记
        xQueueSend(xQueuevirtualKeystate, &recognizer_state, portMAX_DELAY);

        sprintf((char*)SetRECV,"KEY_MENU OK\r\n");
        Uart_Send_Data(SetRECV,strlen((char*)SetRECV));
    }
    else if (strcmp((char *)buff, "key_play") == 0)//eg:KEY_MENU
    {
        
        recognizer_state = myRECOGNIZE;//识别
        xQueueSend(xQueuevirtualKeystate, &recognizer_state, portMAX_DELAY);
       
        sprintf((char*)SetRECV,"KEY_PLAY OK\r\n");
        Uart_Send_Data(SetRECV,strlen((char*)SetRECV));
    }
    else if (strcmp((char *)buff, "key_upup") == 0)//eg:KEY_MENU
    {
        
        recognizer_state = myDETECT;//检测
        xQueueSend(xQueuevirtualKeystate, &recognizer_state, portMAX_DELAY);
       
        sprintf((char*)SetRECV,"KEY_UPUP OK\r\n");
        Uart_Send_Data(SetRECV,strlen((char*)SetRECV));
    }
    else if (strcmp((char *)buff, "key_down") == 0)//eg:KEY_MENU
    {
        
        recognizer_state = myDELETE;//删除
        xQueueSend(xQueuevirtualKeystate, &recognizer_state, portMAX_DELAY);
        sprintf((char*)SetRECV,"KEY_DOWN OK\r\n");
        Uart_Send_Data(SetRECV,strlen((char*)SetRECV));
    }
    else if ((strcmp((char *)buff, "mode_face") == 0) ||
             (strcmp((char *)buff, "m:f") == 0))
    {
        recognizer_state = myRECOGNIZE;
        xQueueSend(xQueuevirtualKeystate, &recognizer_state, portMAX_DELAY);

        sprintf((char*)SetRECV,"MODE FACE OK\r\n");
        Uart_Send_Data(SetRECV,strlen((char*)SetRECV));
    }
    else if ((strcmp((char *)buff, "mode_qr") == 0) ||
             (strcmp((char *)buff, "m:q") == 0))
    {
        recognizer_state = myQRSCAN;
        xQueueSend(xQueuevirtualKeystate, &recognizer_state, portMAX_DELAY);

        sprintf((char*)SetRECV,"MODE QR OK\r\n");
        Uart_Send_Data(SetRECV,strlen((char*)SetRECV));
    }
    else if ((strcmp((char *)buff, "mode_ble") == 0) ||
             (strcmp((char *)buff, "m:b") == 0))
    {
        // BLE mode keeps AI pipeline idle and waits for BLE write command to trigger unlock.
        recognizer_state = myIDLE;
        xQueueSend(xQueuevirtualKeystate, &recognizer_state, portMAX_DELAY);

        sprintf((char*)SetRECV,"MODE BLE OK\r\n");
        Uart_Send_Data(SetRECV,strlen((char*)SetRECV));
    }
    else if ((strcmp((char *)buff, "mode_off") == 0) ||
             (strcmp((char *)buff, "m:o") == 0))
    {
        Uart_Send_Data((uint8_t *)"[UART1 CMD] mode_off\r\n", 23);
        recognizer_state = myIDLE;
        xQueueSend(xQueuevirtualKeystate, &recognizer_state, portMAX_DELAY);

        sprintf((char*)SetRECV,"MODE OFF OK\r\n");
        Uart_Send_Data(SetRECV,strlen((char*)SetRECV));
    }
    else if ((strcmp((char *)buff, "face_add") == 0) ||
             (strcmp((char *)buff, "f:a") == 0))
    {
        recognizer_state = myENROLL;
        xQueueSend(xQueuevirtualKeystate, &recognizer_state, portMAX_DELAY);

        sprintf((char*)SetRECV,"FACE ADD OK\r\n");
        Uart_Send_Data(SetRECV,strlen((char*)SetRECV));
    }
    else if ((strcmp((char *)buff, "face_del") == 0) ||
             (strcmp((char *)buff, "f:d") == 0))
    {
        recognizer_state = myDELETE;
        xQueueSend(xQueuevirtualKeystate, &recognizer_state, portMAX_DELAY);

        sprintf((char*)SetRECV,"FACE DEL OK\r\n");
        Uart_Send_Data(SetRECV,strlen((char*)SetRECV));
    }

     else //单片机不能发送除配置协议以外的任何信息给该模块
    {
        // UART1 now connects to STM32 lock controller.
        // Ignore unknown text (e.g., ACK) to avoid command ping-pong.
        return;
    }


}