/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    dma2d.h
  * @brief   This file contains all the function prototypes for
  *          the dma2d.c file
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
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __DMA2D_H__
#define __DMA2D_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern DMA2D_HandleTypeDef hdma2d;

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

void MX_DMA2D_Init(void);

/* USER CODE BEGIN Prototypes */

HAL_StatusTypeDef TouchGFX_DMA2D_FillRGB888(uint32_t destination,
                                            uint16_t lineOffset,
                                            uint16_t width,
                                            uint16_t height,
                                            uint32_t color);
HAL_StatusTypeDef TouchGFX_DMA2D_CopyRGB888(uint32_t source,
                                            uint32_t destination,
                                            uint16_t width,
                                            uint16_t height);
HAL_StatusTypeDef TouchGFX_DMA2D_CopyRGB888Rect(uint32_t source,
                                                uint32_t destination,
                                                uint16_t x,
                                                uint16_t y,
                                                uint16_t width,
                                                uint16_t height,
                                                uint16_t stridePixels);
uint8_t TouchGFX_DMA2D_IRQHandler(void);

extern volatile uint32_t touchgfx_dma2d_fill_count;
extern volatile uint32_t touchgfx_dma2d_pixel_count;
extern volatile uint32_t touchgfx_dma2d_error_count;
extern volatile uint32_t touchgfx_dma2d_copy_count;
extern volatile uint32_t touchgfx_dma2d_copy_pixel_count;
extern volatile uint32_t touchgfx_dma2d_irq_wait_count;
extern volatile uint32_t touchgfx_dma2d_irq_timeout_count;

void TouchGFX_WaitForDisplayRefresh(void);
void TouchGFX_BeginFullScreenRedraw(void);
uint32_t TouchGFX_GetClientFrameBuffer(void);

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __DMA2D_H__ */

