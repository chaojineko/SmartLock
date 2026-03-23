/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : servo.c
  * @brief          : Servo control module implementation
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
#include "servo.h"
#include "tim.h"
#include "cmsis_os.h"

/* Private defines -----------------------------------------------------------*/
#define SERVO_PWM_MIN           500     /* PWM pulse width for 0 degrees */
#define SERVO_PWM_MAX           2500    /* PWM pulse width for 180 degrees */
#define SERVO_ANGLE_MIN         0.0f
#define SERVO_ANGLE_MAX         180.0f
#define SERVO_LOCK_ANGLE        0.0f
#define SERVO_UNLOCK_ANGLE      90.0f
#define BEEP_PULSE_MS           120U

/**
 * @brief  Initialize servo control with PWM
 * @retval None
 */
void Servo_Init(void)
{
    /* PWM is already started in main.c */
    /* Set to lock position */
    Servo_Lock();
    osDelay(500);
    //Servo_Unlock();
}

/**
 * @brief  Convert angle to PWM compare value and set servo
 * @param  angle: Target angle (0-180 degrees)
 * @retval None
 */
void Servo_SetAngle(float angle)
{
    /* Clamp angle to valid range */
    if (angle < SERVO_ANGLE_MIN) {
        angle = SERVO_ANGLE_MIN;
    } else if (angle > SERVO_ANGLE_MAX) {
        angle = SERVO_ANGLE_MAX;
    }

    /* Convert angle to PWM pulse width (500-2500) */
    uint16_t pulse = (uint16_t)(SERVO_PWM_MIN + 
                                (angle / SERVO_ANGLE_MAX) * 
                                (SERVO_PWM_MAX - SERVO_PWM_MIN));

    /* Set PWM compare value */
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, pulse);
}

/**
 * @brief  Lock servo (0 degrees)
 * @retval None
 */
void Servo_Lock(void)
{
    Servo_SetAngle(SERVO_LOCK_ANGLE);
}

/**
 * @brief  Unlock servo (90 degrees)
 * @retval None
 */
void Servo_Unlock(void)
{
    HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_SET);
    osDelay(BEEP_PULSE_MS);
    HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_RESET);

    Servo_SetAngle(SERVO_UNLOCK_ANGLE);
}
