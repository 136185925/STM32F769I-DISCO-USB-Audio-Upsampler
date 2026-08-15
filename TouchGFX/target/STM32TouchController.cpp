/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : STM32TouchController.cpp
  ******************************************************************************
  * This file was created by TouchGFX Generator 4.26.1. This file is only
  * generated once! Delete this file from your project and re-generate code
  * using STM32CubeMX or change this file manually to update it.
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

/* USER CODE BEGIN STM32TouchController */

#include <STM32TouchController.hpp>

extern "C"
{
#include "touch.h"
volatile uint32_t touchgfx_touch_sample_count = 0U;
}

void STM32TouchController::init()
{
    (void)Touch_Init();
}

bool STM32TouchController::sampleTouch(int32_t& x, int32_t& y)
{
    uint16_t touchX;
    uint16_t touchY;

    ++touchgfx_touch_sample_count;
    if (Touch_GetPoint(&touchX, &touchY) == 0U)
    {
        return false;
    }

    x = touchX;
    y = touchY;
    return true;
}

/* USER CODE END STM32TouchController */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
