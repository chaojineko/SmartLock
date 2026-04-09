#include "uart_debug.h"
#include "usart.h"  // CubeMX-generated USART init and handle definitions
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

// 关闭半主机模式（Keil）
#pragma import(__use_no_semihosting)

struct __FILE {
    int handle;
};

FILE __stdout;

void _sys_exit(int x) {
    (void)x;
}

// 通过UART1重定向 printf
int fputc(int ch, FILE *f) {
    (void)f;
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}

// 发送接口
void uart1_putc(char ch) {
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
}

void uart1_puts(const char *s) {
    HAL_UART_Transmit(&huart1, (uint8_t *)s, strlen(s), HAL_MAX_DELAY);
}

void uart2_putc(char ch) {
    HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
}

void uart2_puts(const char *s) {
    HAL_UART_Transmit(&huart2, (uint8_t *)s, strlen(s), HAL_MAX_DELAY);
}

void uart3_putc(char ch) {
    HAL_UART_Transmit(&huart3, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
}

void uart3_puts(const char *s) {
    HAL_UART_Transmit(&huart3, (uint8_t *)s, strlen(s), HAL_MAX_DELAY);
}

// 接收缓冲区（每个UART一个，USART2由sfm.c处理，因此此处不处理USART2）
volatile uint8_t uart1_rx_ready = 0;

// USART2 由 sfm.c 专门处理，此处不再处理
// static uint8_t uart2_rx_char;
// static uart_rx_buf uart2_rx_buf = {0};
// volatile uint8_t uart2_rx_ready = 0;

volatile uint8_t uart3_rx_ready = 0;

// 初始化（在 main 里调用）
// 注意：此版本不启动任何 UART 接收中断
// 所有 UART 接收由各专用驱动处理（如 sfm.c 处理 USART2）
void uart_debug_init(void) {
    /* 
    NOTE: uart_debug 模块仅实现 UART 发送和 printf 重定向
    不启动接收中断，避免与其他驱动冲突
    */
}

/* NOTE: HAL_UART_RxCpltCallback 已移至 sfm.c 统一处理所有 UART 中断 */
/* 详见 Core/Src/sfm.c 中的 HAL_UART_RxCpltCallback() 函数 */

// 串口输出测试（调用此函数即可在串口助手看到文本）
void uart_debug_test(void) {
    printf("[UART DEBUG] Hello from STM32F407\r\n");
    uart1_puts("uart1_puts(): Hello world\r\n");
}

// 解析命令（示例，需根据需求调整）
void parse_cmd(char *cmd) {
    if (strstr(cmd, "ledon")) {
        printf("LED ON!\r\n");
        // HAL_GPIO_WritePin(GPIOx, GPIO_PIN_x, GPIO_PIN_RESET);
    } else if (strstr(cmd, "ledoff")) {
        printf("LED OFF!\r\n");
        // HAL_GPIO_WritePin(GPIOx, GPIO_PIN_x, GPIO_PIN_SET);
    }
}
