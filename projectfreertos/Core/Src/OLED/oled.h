#ifndef __OLED_H
#define __OLED_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h" // 包含HAL库和基本配置
#include "cmsis_os.h" // 包含FreeRTOS的CMSIS-RTOS API
#include "stdlib.h"

/*
 * ！！！请注意！！！
 * 下方的引脚定义是根据您原始文件注释的占位符。
 * 请务必根据您在CubeMX中的实际配置来修改这些定义。
 * 例如:
 * #define OLED_SCL_GPIO_Port  GPIOA
 * #define OLED_SCL_Pin        GPIO_PIN_5
 * #define OLED_SDA_GPIO_Port  GPIOA
 * #define OLED_SDA_Pin        GPIO_PIN_7
 */
#define OLED_SCL_GPIO_Port  GPIOD
#define OLED_SCL_Pin        GPIO_PIN_6
#define OLED_SDA_GPIO_Port  GPIOD
#define OLED_SDA_Pin        GPIO_PIN_7

// OLED I2C 模拟宏定义
#define OLED_SCL_Set()      HAL_GPIO_WritePin(OLED_SCL_GPIO_Port, OLED_SCL_Pin, GPIO_PIN_SET)
#define OLED_SCL_Clr()      HAL_GPIO_WritePin(OLED_SCL_GPIO_Port, OLED_SCL_Pin, GPIO_PIN_RESET)
#define OLED_SDA_Set()      HAL_GPIO_WritePin(OLED_SDA_GPIO_Port, OLED_SDA_Pin, GPIO_PIN_SET)
#define OLED_SDA_Clr()      HAL_GPIO_WritePin(OLED_SDA_GPIO_Port, OLED_SDA_Pin, GPIO_PIN_RESET)
#define OLED_SDA_Read()     HAL_GPIO_ReadPin(OLED_SDA_GPIO_Port, OLED_SDA_Pin)

#define OLED_CMD  0	// 写命令
#define OLED_DATA 1	// 写数据

// 为了兼容旧的函数签名，定义类型别名
#define u8 uint8_t
#define u32 uint32_t

// OLED控制用函数
void OLED_Init(void);
void OLED_Clear(void);
void OLED_Display_On(void);
void OLED_Display_Off(void);
void OLED_Set_Pos(unsigned char x, unsigned char y);
void OLED_WR_Byte(uint8_t dat, uint8_t cmd);

// 绘图函数
void OLED_DrawPoint(u8 x,u8 y,u8 t);
void OLED_Fill(u8 x1,u8 y1,u8 x2,u8 y2,u8 dot);
void OLED_ShowChar(u8 x,u8 y,u8 chr,u8 Char_Size);
void OLED_ShowNum(u8 x,u8 y,u32 num,u8 len,u8 size);
void OLED_ShowString(u8 x,u8 y, u8 *p,u8 Char_Size);
void OLED_ShowCHinese(u8 x,u8 y,u8 no);
void OLED_DrawBMP(unsigned char x0, unsigned char y0,unsigned char x1, unsigned char y1,unsigned char BMP[]);
void fill_picture(unsigned char fill_Data);


#ifdef __cplusplus
}
#endif

#endif
