/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : keyboard.c
  * @brief          : Keyboard (4x4 keypad) control module implementation
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
#include "keyboard.h"
#include "cmsis_os.h"

/* Private defines -----------------------------------------------------------*/
/* 4x4 keypad matrix mapping (row x col) */
static const char keypad_map[4][4] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'}
};

/**
 * @brief  Get character corresponding to key index
 * @param  index: Key index (0-15)
 * @retval Character of the key
 */
char Keypad_GetChar(int index)
{
    if (index < 0 || index >= (KEYPAD_ROWS * KEYPAD_COLS)) {
        return '\0';
    }
    
    int row = index / KEYPAD_COLS;
    int col = index % KEYPAD_COLS;
    return keypad_map[row][col];
}

/**
 * @brief  Scan the keypad matrix for pressed keys
 *         Uses row-by-row scanning technique
 * @retval Key index (0-15) if a key is pressed, -1 if no key is pressed
 */
int Keypad_Scan(void)
{
    int key = KEYPAD_NO_KEY;

    /* Set all rows high (default unpressed state) */
    HAL_GPIO_WritePin(GPIOD, R1_Pin | R2_Pin | R3_Pin | R4_Pin, GPIO_PIN_SET);

    for (int row = 0; row < KEYPAD_ROWS; row++) {
        /* Get row pin for current row */
        uint16_t rowPin = (row == 0) ? R1_Pin :
                          (row == 1) ? R2_Pin :
                          (row == 2) ? R3_Pin : R4_Pin;

        /* Select current row: pull low */
        HAL_GPIO_WritePin(GPIOD, rowPin, GPIO_PIN_RESET);
        osDelay(1);  /* Let the pin stabilize */

        /* Column port and pin mapping */
        GPIO_TypeDef *colPorts[4] = {C1_GPIO_Port, C3_GPIO_Port, C2_GPIO_Port, C4_GPIO_Port};
        uint16_t colPins[4] = {C1_Pin, C3_Pin, C2_Pin, C4_Pin};

        /* Scan columns */
        for (int col = 0; col < KEYPAD_COLS; col++) {
            if (HAL_GPIO_ReadPin(colPorts[col], colPins[col]) == GPIO_PIN_RESET) {
                key = row * KEYPAD_COLS + col;
                break;
            }
        }

        /* Restore current row to high */
        HAL_GPIO_WritePin(GPIOD, rowPin, GPIO_PIN_SET);

        if (key != KEYPAD_NO_KEY) {
            break;
        }
    }

    return key;
}
