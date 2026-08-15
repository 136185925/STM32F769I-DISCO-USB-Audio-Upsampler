/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    rtc.c
  * @brief   This file provides code for the configuration
  *          of the RTC instances.
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
#include "rtc.h"

/* USER CODE BEGIN 0 */

#define STOPWATCH_LAP_CAPACITY 16U
#define STOPWATCH_BUTTON_DEBOUNCE_TICKS ((RTC_LSE_TICK_HZ + 19U) / 20U)

static volatile uint32_t stopwatch_ticks = 0U;
static volatile uint8_t stopwatch_running = 0U;
static uint32_t stopwatch_laps[STOPWATCH_LAP_CAPACITY] = {0U};
static uint32_t stopwatch_lap_totals[STOPWATCH_LAP_CAPACITY] = {0U};
static uint8_t stopwatch_lap_numbers[STOPWATCH_LAP_CAPACITY] = {0U};
static uint32_t stopwatch_last_lap_tick = 0U;
static uint8_t stopwatch_lap_count = 0U;
static uint8_t stopwatch_lap_total = 0U;
static uint32_t stopwatch_user_button_last_tick = 0U;
static uint8_t stopwatch_user_button_seen = 0U;

volatile uint32_t stopwatch_lse_irq_count = 0U;
volatile uint32_t stopwatch_control_generation = 0U;
volatile uint32_t stopwatch_user_button_press_count = 0U;
volatile uint32_t stopwatch_user_button_debounce_count = 0U;

/* USER CODE END 0 */

RTC_HandleTypeDef hrtc;

/* RTC init function */
void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};
  RTC_AlarmTypeDef sAlarm = {0};

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN Check_RTC_BKUP */

  /* USER CODE END Check_RTC_BKUP */

  /** Initialize RTC and set the Time and Date
  */
  sTime.Hours = 0x0;
  sTime.Minutes = 0x0;
  sTime.Seconds = 0x0;
  sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  sTime.StoreOperation = RTC_STOREOPERATION_RESET;
  if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  sDate.WeekDay = RTC_WEEKDAY_MONDAY;
  sDate.Month = RTC_MONTH_JANUARY;
  sDate.Date = 0x1;
  sDate.Year = 0x0;
  if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable the Alarm A
  */
  sAlarm.AlarmTime.Hours = 0x0;
  sAlarm.AlarmTime.Minutes = 0x0;
  sAlarm.AlarmTime.Seconds = 0x0;
  sAlarm.AlarmTime.SubSeconds = 0x0;
  sAlarm.AlarmTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  sAlarm.AlarmTime.StoreOperation = RTC_STOREOPERATION_RESET;
  sAlarm.AlarmMask = RTC_ALARMMASK_NONE;
  sAlarm.AlarmSubSecondMask = RTC_ALARMSUBSECONDMASK_ALL;
  sAlarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_DATE;
  sAlarm.AlarmDateWeekDay = 0x1;
  sAlarm.Alarm = RTC_ALARM_A;
  if (HAL_RTC_SetAlarm(&hrtc, &sAlarm, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable the Alarm B
  */
  sAlarm.Alarm = RTC_ALARM_B;
  if (HAL_RTC_SetAlarm(&hrtc, &sAlarm, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */

  /* 32768 Hz LSE / 16 / (1 + 1) = 1024 Hz. This gives an exact binary
     hardware timebase, independent of the FreeRTOS and TouchGFX tick rates. */
  if (HAL_RTCEx_DeactivateWakeUpTimer(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, 1U,
                                  RTC_WAKEUPCLOCK_RTCCLK_DIV16) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE END RTC_Init 2 */

}

void HAL_RTC_MspInit(RTC_HandleTypeDef* rtcHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(rtcHandle->Instance==RTC)
  {
  /* USER CODE BEGIN RTC_MspInit 0 */

  /* USER CODE END RTC_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    PeriphClkInitStruct.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    /* RTC clock enable */
    __HAL_RCC_RTC_ENABLE();

    HAL_NVIC_SetPriority(RTC_WKUP_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(RTC_WKUP_IRQn);

    __HAL_RCC_GPIOC_CLK_ENABLE();
    /**RTC GPIO Configuration
    PC13     ------> RTC_OUT
    */
    GPIO_InitStruct.Pin = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /* USER CODE BEGIN RTC_MspInit 1 */

  /* USER CODE END RTC_MspInit 1 */
  }
}

void HAL_RTC_MspDeInit(RTC_HandleTypeDef* rtcHandle)
{

  if(rtcHandle->Instance==RTC)
  {
  /* USER CODE BEGIN RTC_MspDeInit 0 */

  /* USER CODE END RTC_MspDeInit 0 */
    /* Peripheral clock disable */
    HAL_NVIC_DisableIRQ(RTC_WKUP_IRQn);
    __HAL_RCC_RTC_DISABLE();

    /**RTC GPIO Configuration
    PC13     ------> RTC_OUT
    */
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_13);

  /* USER CODE BEGIN RTC_MspDeInit 1 */

  /* USER CODE END RTC_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

void HAL_RTCEx_WakeUpTimerEventCallback(RTC_HandleTypeDef *rtc)
{
  if (rtc != &hrtc)
  {
    return;
  }

  ++stopwatch_lse_irq_count;
  if (stopwatch_running != 0U)
  {
    ++stopwatch_ticks;
  }
}

void Stopwatch_Toggle(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  stopwatch_running = (stopwatch_running == 0U) ? 1U : 0U;
  ++stopwatch_control_generation;
  __DMB();
  if (primask == 0U)
  {
    __enable_irq();
  }
}

void Stopwatch_UserButtonIRQ(void)
{
  uint32_t now = stopwatch_lse_irq_count;

  /* PA0 is rising-edge triggered. Suppress contact bounce using the same
     continuously running 1024 Hz LSE timebase as the stopwatch. */
  if ((stopwatch_user_button_seen != 0U) &&
      ((uint32_t)(now - stopwatch_user_button_last_tick) <
       STOPWATCH_BUTTON_DEBOUNCE_TICKS))
  {
    ++stopwatch_user_button_debounce_count;
    return;
  }

  stopwatch_user_button_seen = 1U;
  stopwatch_user_button_last_tick = now;
  ++stopwatch_user_button_press_count;
  Stopwatch_Toggle();
}

uint32_t RTC_LSE_GetTicks(void)
{
  return stopwatch_lse_irq_count;
}

void Stopwatch_Reset(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  stopwatch_running = 0U;
  stopwatch_ticks = 0U;
  stopwatch_last_lap_tick = 0U;
  stopwatch_lap_count = 0U;
  stopwatch_lap_total = 0U;
  for (uint8_t i = 0U; i < STOPWATCH_LAP_CAPACITY; ++i)
  {
    stopwatch_laps[i] = 0U;
    stopwatch_lap_totals[i] = 0U;
    stopwatch_lap_numbers[i] = 0U;
  }
  if (primask == 0U)
  {
    __enable_irq();
  }
}

uint8_t Stopwatch_Lap(void)
{
  if (stopwatch_running == 0U)
  {
    return 0U;
  }

  uint32_t now = stopwatch_ticks;
  uint32_t interval = now - stopwatch_last_lap_tick;
  for (uint8_t i = STOPWATCH_LAP_CAPACITY - 1U; i > 0U; --i)
  {
    stopwatch_laps[i] = stopwatch_laps[i - 1U];
    stopwatch_lap_totals[i] = stopwatch_lap_totals[i - 1U];
    stopwatch_lap_numbers[i] = stopwatch_lap_numbers[i - 1U];
  }
  ++stopwatch_lap_total;
  stopwatch_laps[0] = interval;
  stopwatch_lap_totals[0] = now;
  stopwatch_lap_numbers[0] = stopwatch_lap_total;
  stopwatch_last_lap_tick = now;
  if (stopwatch_lap_count < STOPWATCH_LAP_CAPACITY)
  {
    ++stopwatch_lap_count;
  }
  return 1U;
}

uint32_t Stopwatch_GetTicks(void)
{
  return stopwatch_ticks;
}

uint8_t Stopwatch_IsRunning(void)
{
  return stopwatch_running;
}

uint8_t Stopwatch_GetLapCount(void)
{
  return stopwatch_lap_count;
}

uint8_t Stopwatch_GetLapNumber(uint8_t index)
{
  return (index < stopwatch_lap_count) ? stopwatch_lap_numbers[index] : 0U;
}

uint32_t Stopwatch_GetLapTicks(uint8_t index)
{
  return (index < stopwatch_lap_count) ? stopwatch_laps[index] : 0U;
}

uint32_t Stopwatch_GetLapTotalTicks(uint8_t index)
{
  return (index < stopwatch_lap_count) ? stopwatch_lap_totals[index] : 0U;
}

/* USER CODE END 1 */

