/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : keyboard.h
  * @brief          : Header for keyboard (4x4 keypad) control module
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

#ifndef __KEYBOARD_H__
#define __KEYBOARD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* Keypad configuration */
#define KEYPAD_ROWS     4
#define KEYPAD_COLS     4
#define KEYPAD_NO_KEY   -1

/**
 * @brief  Get the character corresponding to a key index
 * @param  index: Key index (0-15)
 * @retval Character representation of the key
 */
char Keypad_GetChar(int index);

/**
 * @brief  Scan the 4x4 keypad matrix for pressed keys
 * @retval Key index (0-15) if a key is pressed, -1 if no key is pressed
 */
int Keypad_Scan(void);

#ifdef __cplusplus
}
#endif

#endif /* __KEYBOARD_H__ */
