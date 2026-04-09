#include "stdio.h"
#include "my_usart.h"
#include "string.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "driver/gpio.h"

static const char *UARTTAG = "UART";

extern uint16_t wifi_Mode;    //wifi模式 0:AP  1:sta  2:AP+STA
extern uint8_t sta_ip_connect[4]; //这个是sta_ip相关的


static QueueHandle_t xQueuevirtualKeystate = NULL; //虚拟按键
static myrecognizer_state_t recognizer_state = myDETECT;//人脸检测的标志,默认就是检测

static mycolor_detection_state_t mydetector_state = myCOLOR_DETECTION_IDLE;//颜色识别的标志


const int uart_buffer_size = (1024 * 2);


// 串口接收和发送任务 
static void My_Uart_Task(void *arg)
{
    //ESP_LOGI(UARTTAG, "Start Uart0_Rx_Task with core:%d", xPortGetCoreID());
    uint16_t temp_len = 255;
    uint8_t* temp_data = (uint8_t*) malloc(temp_len);
    

    while (1) 
    {
        // 从串口0读取数据 
        const int rxBytes = uart_read_bytes(UART_MUN, temp_data, temp_len, 1);  // /portTICK_PERIOD_MS
        if (rxBytes > 0)
        {
           //Uart_Send_Data(temp_data,rxBytes);//直接发送(回显)
           Deal_uart_massage(temp_data,rxBytes);
           uart_flush(UART_MUN);//清除它的接收缓存
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
    free(temp_data);
    vTaskDelete(NULL);
}


// 通过串口发送一串数据 
int Uart_Send_Data(uint8_t* data, uint16_t len)
{
    const int txBytes = uart_write_bytes(UART_MUN, data, len);
    return txBytes;
}

// 通过串口发送一个字节 
int Uart_Send_Byte(uint8_t data)
{
    uint8_t data1 = data;
    const int txBytes = uart_write_bytes(UART_MUN, &data1, 1);
    return txBytes;
}


// 初始化串口, 波特率为115200 
//后期可能要加模式选择标志按键 -尽可能的合在一起
void My_Uart_Init(const QueueHandle_t key_state_i)
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
    uart_driver_install(UART_MUN, uart_buffer_size, uart_buffer_size, 0,NULL, 0);
    uart_param_config(UART_MUN, &uart_config);
    uart_set_pin(UART_MUN, UART_GPIO_TXD, UART_GPIO_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    
    //printf("uart_init_ok\r\n");
    xTaskCreate(My_Uart_Task, "Uart_my_Task", 5*1024, NULL, 5, NULL);
}



static uint8_t SetRECV[40]="\0";
void Deal_uart_massage(uint8_t *buff,uint16_t len)//处理串口接收到的数据
{
    uint8_t data[len];
    uint8_t  length = 0;


    //打印ip的函数
    if ((strncmp("sta_ip",(char*)buff,6)==0) ||(strncmp("STA_IP",(char*)buff,6)==0))
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
    

    //打印ip的函数
    else if ((strncmp("ap_ip",(char*)buff,5)==0) ||(strncmp("AP_IP",(char*)buff,5)==0))
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
        Get_Key_massage(buff,len);
    }
    memset(SetRECV,0,sizeof(SetRECV));//清空它

}


bool register_mode = false;//颜色识别的注册标志

void Get_Key_massage(uint8_t *buff,uint16_t len)
{
    uint8_t data[len];
    uint8_t  length = 0;

    if ((strncmp("key_menu",(char*)buff,8)==0) ||(strncmp("KEY_MENU",(char*)buff,8)==0))//eg:KEY_MENU
    {
        // //人脸识别
       
        recognizer_state = myENROLL;//登记
        xQueueSend(xQueuevirtualKeystate, &recognizer_state, portMAX_DELAY);

        sprintf((char*)SetRECV,"KEY_MENU OK\r\n");
        Uart_Send_Data(SetRECV,strlen((char*)SetRECV)); //回传设置OK
    }
    else if ((strncmp("key_play",(char*)buff,8)==0) ||(strncmp("KEY_PLAY",(char*)buff,8)==0))//eg:KEY_MENU
    {
        
        recognizer_state = myRECOGNIZE;//识别
        xQueueSend(xQueuevirtualKeystate, &recognizer_state, portMAX_DELAY);
       
        sprintf((char*)SetRECV,"KEY_PLAY OK\r\n");
        Uart_Send_Data(SetRECV,strlen((char*)SetRECV)); //回传设置OK
    }
    else if ((strncmp("key_upup",(char*)buff,8)==0) ||(strncmp("KEY_UPUP",(char*)buff,8)==0))//eg:KEY_MENU
    {
        
        recognizer_state = myDETECT;//检测
        xQueueSend(xQueuevirtualKeystate, &recognizer_state, portMAX_DELAY);
       
        sprintf((char*)SetRECV,"KEY_UPUP OK\r\n");
        Uart_Send_Data(SetRECV,strlen((char*)SetRECV)); //回传设置OK
    }
    else if ((strncmp("key_down",(char*)buff,8)==0) ||(strncmp("KEY_DOWN",(char*)buff,8)==0))//eg:KEY_MENU
    {
        
        recognizer_state = myDELETE;//删除
        xQueueSend(xQueuevirtualKeystate, &recognizer_state, portMAX_DELAY);
        sprintf((char*)SetRECV,"KEY_DOWN OK\r\n");
        Uart_Send_Data(SetRECV,strlen((char*)SetRECV)); //回传设置OK
    }
    else if ((strncmp("mode_face", (char*)buff, 9) == 0) ||
             (strncmp("MODE_FACE", (char*)buff, 9) == 0) ||
             (strncmp("m:f", (char*)buff, 3) == 0) ||
             (strncmp("M:F", (char*)buff, 3) == 0))
    {
        recognizer_state = myRECOGNIZE;
        xQueueSend(xQueuevirtualKeystate, &recognizer_state, portMAX_DELAY);

        sprintf((char*)SetRECV,"MODE FACE OK\r\n");
        Uart_Send_Data(SetRECV,strlen((char*)SetRECV));
    }
    else if ((strncmp("mode_qr", (char*)buff, 7) == 0) ||
             (strncmp("MODE_QR", (char*)buff, 7) == 0) ||
             (strncmp("m:q", (char*)buff, 3) == 0) ||
             (strncmp("M:Q", (char*)buff, 3) == 0))
    {
        recognizer_state = myQRSCAN;
        xQueueSend(xQueuevirtualKeystate, &recognizer_state, portMAX_DELAY);

        sprintf((char*)SetRECV,"MODE QR OK\r\n");
        Uart_Send_Data(SetRECV,strlen((char*)SetRECV));
    }

     else //单片机不能发送除配置协议以外的任何信息给该模块
    {
        sprintf((char*)SetRECV,"fail,command no found!\r\n");
        Uart_Send_Data(SetRECV,strlen((char*)SetRECV)); //回传
    }


}