#ifndef _UART_DEBUG_H_
#define _UART_DEBUG_H_

#include "stm32f4xx_hal.h"
#include "cmsis_os.h"

typedef struct
{
    uint8_t uart_buf[64];
    uint32_t uart_cnt;
} uart_rx_buf;

// UART句柄（由CubeMX生成）
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;

// 接收完成标志（可用于主任务），ISR 里置位
extern volatile uint8_t uart1_rx_ready;
extern volatile uint8_t uart2_rx_ready;
extern volatile uint8_t uart3_rx_ready;

void uart_debug_init(void);
void uart_debug_test(void);

void uart1_putc(char ch);
void uart1_puts(const char *s);
void uart2_putc(char ch);
void uart2_puts(const char *s);
void uart3_putc(char ch);
void uart3_puts(const char *s);

void parse_cmd(char *cmd);

#endif
