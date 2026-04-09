/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : servo.h
  * @brief          : Header for servo control module
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

#ifndef __SERVO_H__
#define __SERVO_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/**
 * @brief  Initialize servo control (sets servo to lock position)
 * @retval None
 */
void Servo_Init(void);

/**
 * @brief  Set servo angle (0-180 degrees)
 * @param  angle: Target angle in degrees (0.0 - 180.0)
 * @retval None
 */
void Servo_SetAngle(float angle);

/**
 * @brief  Lock the servo (0 degrees)
 * @retval None
 */
void Servo_Lock(void);

/**
 * @brief  Unlock the servo (90 degrees)
 * @retval None
 */
void Servo_Unlock(void);

#ifdef __cplusplus
}
#endif

#endif /* __SERVO_H__ */
