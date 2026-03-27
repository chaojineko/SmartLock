/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "menu.h"  // 添加UI任务头文件
#include "rc522.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "usart.h"
#include "servo.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define UART3_LOG_RX_BYTES 0

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
void RFID_Test_Task(void *argument);  // RFID测试任务声明
void ESP32_UART3_Task(void *argument); // ESP32串口命令任务
/* All keyboard and servo logic has been moved to respective modules */

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* Servo and keyboard functions are now in servo.c and keyboard.c modules */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
    
  // 创建UI任务
  if (xTaskCreate(Task_UI, "Task_UI", 256, NULL, 5, NULL) != pdPASS)
  {
    printf("[RTOS] Task_UI create failed\r\n");
  }

  // ESP32 <-> STM32 串口通信任务（USART3）
  // 提高优先级，避免高负载时错过短命令帧
  if (xTaskCreate(ESP32_UART3_Task, "ESP32_UART3", 256, NULL, 4, NULL) != pdPASS)
  {
    printf("[RTOS] ESP32_UART3 create failed\r\n");
  }
  
    // 注意：菜单任务已负责RFID流程，这里不再创建测试任务，避免并发访问RC522/SPI造成读卡失败
    // xTaskCreate(RFID_Test_Task, "RFID_Test", 512, NULL, 4, NULL);
  
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /* Default task can be used for other purposes or remain idle.
     All RFID logic has been moved to RFID_Test_Task. */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1000);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
// RFID测试任务
void RFID_Test_Task(void *argument)
{
  // 1. 初始化RFID模块
  RFID_Init();
  RFID_ClearStoredCards(); // 清除所有存储的卡片（用于测试）
  printf("RFID Task Started and Module Initialized!\r\n");

  // 2. 循环检测卡片
  for(;;)
  {
    printf("\r\n--- Running RFID Scan ---\r\n");
    // 调用高级演示函数，它会处理寻卡、读卡、打印ID等
    RFID_Demo();

    // 任务延时，让出CPU给其他任务
    // 每3秒执行一次完整的扫描/演示流程
    osDelay(3000);
  }
}

static void ESP32_UART3_ProcessCmd(char *cmd,
                                   uint32_t *last_unlock_tick,
                                   uint32_t unlock_hold_ms,
                                   uint8_t *is_unlocked)
{
  size_t start = 0U;
  size_t end;
  size_t i;
  uint32_t now_tick;

  if ((cmd == NULL) || (last_unlock_tick == NULL) || (is_unlocked == NULL))
  {
    return;
  }

  end = strlen(cmd);
  while ((start < end) && ((cmd[start] == ' ') || (cmd[start] == '\t')))
  {
    start++;
  }

  while ((end > start) && ((cmd[end - 1U] == ' ') || (cmd[end - 1U] == '\t')))
  {
    cmd[end - 1U] = '\0';
    end--;
  }

  if (start > 0U)
  {
    memmove(cmd, &cmd[start], (end - start) + 1U);
  }

  for (i = 0U; cmd[i] != '\0'; i++)
  {
    cmd[i] = (char)toupper((unsigned char)cmd[i]);
  }

  if (cmd[0] == '\0')
  {
    return;
  }

  if ((strcmp(cmd, "UNLOCK") == 0) || (strcmp(cmd, "U") == 0))
  {
    Servo_Unlock();
    *last_unlock_tick = HAL_GetTick();
    *is_unlocked = 1U;
    HAL_UART_Transmit(&huart3, (uint8_t *)"ACK:UNLOCK\r\n", 12, 100);
    printf("[ESP32 CMD] UNLOCK\r\n");
  }
  else if ((strcmp(cmd, "LOCK") == 0) || (strcmp(cmd, "L") == 0))
  {
    now_tick = HAL_GetTick();
    if ((now_tick - *last_unlock_tick) >= unlock_hold_ms)
    {
      Servo_Lock();
      *is_unlocked = 0U;
      HAL_UART_Transmit(&huart3, (uint8_t *)"ACK:LOCK\r\n", 10, 100);
      printf("[ESP32 CMD] LOCK\r\n");
    }
    else
    {
      HAL_UART_Transmit(&huart3, (uint8_t *)"ACK:LOCK:HOLD\r\n", 15, 100);
      printf("[ESP32 CMD] LOCK ignored (hold)\r\n");
    }
  }
  else if ((strcmp(cmd, "PING") == 0) || (strcmp(cmd, "P") == 0))
  {
    HAL_UART_Transmit(&huart3, (uint8_t *)"ACK:PING\r\n", 10, 100);
    printf("[ESP32 CMD] PING\r\n");
  }
  else
  {
    HAL_UART_Transmit(&huart3, (uint8_t *)"ERR:CMD\r\n", 9, 100);
    printf("[ESP32 CMD] unknown: %s\r\n", cmd);
  }
}

void ESP32_UART3_Task(void *argument)
{
  uint8_t rx;
  HAL_StatusTypeDef rx_status;
  char cmd[32] = {0};
  uint8_t idx = 0;
  uint8_t is_unlocked = 0U;
  uint32_t last_rx_tick = 0;
  uint32_t last_unlock_tick = 0;
  const char *boot_msg = "STM32_UART3_READY\r\n";
  const uint32_t unlock_hold_ms = 5000U;
  const uint32_t inter_byte_timeout_ms = 500U;

  HAL_UART_Transmit(&huart3, (uint8_t *)boot_msg, (uint16_t)strlen(boot_msg), 100);
  printf("[ESP32 UART3] task ready, waiting commands...\r\n");

  for (;;)
  {
    rx_status = HAL_UART_Receive(&huart3, &rx, 1, 20);

    if (rx_status == HAL_OK)
    {
      uint32_t now = HAL_GetTick();

      if ((idx > 0U) && ((now - last_rx_tick) > inter_byte_timeout_ms))
      {
        cmd[idx] = '\0';
        ESP32_UART3_ProcessCmd(cmd, &last_unlock_tick, unlock_hold_ms, &is_unlocked);
        idx = 0;
        memset(cmd, 0, sizeof(cmd));
      }

      last_rx_tick = now;
    #if UART3_LOG_RX_BYTES
      printf("[UART3 RX] 0x%02X\r\n", rx);
    #endif

      if ((rx == '\r') || (rx == '\n'))
      {
        if (idx > 0)
        {
          cmd[idx] = '\0';
          ESP32_UART3_ProcessCmd(cmd, &last_unlock_tick, unlock_hold_ms, &is_unlocked);

          idx = 0;
          memset(cmd, 0, sizeof(cmd));
        }
      }
      else if (idx < (sizeof(cmd) - 1U))
      {
        cmd[idx++] = (char)rx;
      }
      else
      {
        idx = 0;
        memset(cmd, 0, sizeof(cmd));
        HAL_UART_Transmit(&huart3, (uint8_t *)"ERR:LEN\r\n", 9, 100);
      }
    }
    else if (rx_status == HAL_ERROR)
    {
      uint32_t err = HAL_UART_GetError(&huart3);
      if (err != HAL_UART_ERROR_NONE)
      {
        // Clear sticky UART errors (especially ORE) so receive can recover.
        __HAL_UART_CLEAR_PEFLAG(&huart3);
        __HAL_UART_CLEAR_FEFLAG(&huart3);
        __HAL_UART_CLEAR_NEFLAG(&huart3);
        __HAL_UART_CLEAR_OREFLAG(&huart3);
        printf("[UART3 ERR] 0x%08lX\r\n", err);
      }
      idx = 0;
      memset(cmd, 0, sizeof(cmd));
    }

    if (is_unlocked != 0U)
    {
      uint32_t now_tick = HAL_GetTick();
      if ((now_tick - last_unlock_tick) >= unlock_hold_ms)
      {
        Servo_Lock();
        is_unlocked = 0U;
        HAL_UART_Transmit(&huart3, (uint8_t *)"ACK:AUTOLOCK\r\n", 13, 100);
        printf("[ESP32 CMD] AUTO LOCK\r\n");
      }
    }

    osDelay(5);
  }
}
/* USER CODE END Application */

