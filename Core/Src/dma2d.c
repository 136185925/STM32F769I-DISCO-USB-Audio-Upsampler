/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    dma2d.c
  * @brief   This file provides code for the configuration
  *          of the DMA2D instances.
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
#include "dma2d.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

DMA2D_HandleTypeDef hdma2d;

volatile uint32_t touchgfx_dma2d_fill_count = 0U;
volatile uint32_t touchgfx_dma2d_pixel_count = 0U;
volatile uint32_t touchgfx_dma2d_error_count = 0U;
volatile uint32_t touchgfx_dma2d_copy_count = 0U;
volatile uint32_t touchgfx_dma2d_copy_pixel_count = 0U;
volatile uint32_t touchgfx_dma2d_irq_wait_count = 0U;
volatile uint32_t touchgfx_dma2d_irq_timeout_count = 0U;

#define TOUCHGFX_DMA2D_IRQ_THRESHOLD_PIXELS  32768UL
#define TOUCHGFX_DMA2D_IRQ_TIMEOUT_MS         20U

static StaticSemaphore_t touchgfx_dma2d_semaphore_control;
static SemaphoreHandle_t touchgfx_dma2d_semaphore;
static volatile uint8_t touchgfx_dma2d_custom_irq_active;
static volatile HAL_StatusTypeDef touchgfx_dma2d_custom_status = HAL_ERROR;

/* DMA2D init function */
void MX_DMA2D_Init(void)
{

  /* USER CODE BEGIN DMA2D_Init 0 */

  /* USER CODE END DMA2D_Init 0 */

  /* USER CODE BEGIN DMA2D_Init 1 */

  /* USER CODE END DMA2D_Init 1 */
  hdma2d.Instance = DMA2D;
  hdma2d.Init.Mode = DMA2D_R2M;
  hdma2d.Init.ColorMode = DMA2D_OUTPUT_RGB888;
  hdma2d.Init.OutputOffset = 0;
  if (HAL_DMA2D_Init(&hdma2d) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN DMA2D_Init 2 */

  touchgfx_dma2d_semaphore =
      xSemaphoreCreateBinaryStatic(&touchgfx_dma2d_semaphore_control);

  /* USER CODE END DMA2D_Init 2 */

}

void HAL_DMA2D_MspInit(DMA2D_HandleTypeDef* dma2dHandle)
{

  if(dma2dHandle->Instance==DMA2D)
  {
  /* USER CODE BEGIN DMA2D_MspInit 0 */

  /* USER CODE END DMA2D_MspInit 0 */
    /* DMA2D clock enable */
    __HAL_RCC_DMA2D_CLK_ENABLE();

    /* DMA2D interrupt Init */
    HAL_NVIC_SetPriority(DMA2D_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA2D_IRQn);
  /* USER CODE BEGIN DMA2D_MspInit 1 */

  /* USER CODE END DMA2D_MspInit 1 */
  }
}

void HAL_DMA2D_MspDeInit(DMA2D_HandleTypeDef* dma2dHandle)
{

  if(dma2dHandle->Instance==DMA2D)
  {
  /* USER CODE BEGIN DMA2D_MspDeInit 0 */

  /* USER CODE END DMA2D_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_DMA2D_CLK_DISABLE();

    /* DMA2D interrupt Deinit */
    HAL_NVIC_DisableIRQ(DMA2D_IRQn);
  /* USER CODE BEGIN DMA2D_MspDeInit 1 */

  /* USER CODE END DMA2D_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

static uint8_t TouchGFX_DMA2D_UseInterrupt(uint32_t pixelCount)
{
  return ((__get_IPSR() == 0U) &&
          (pixelCount >= TOUCHGFX_DMA2D_IRQ_THRESHOLD_PIXELS) &&
          (touchgfx_dma2d_semaphore != NULL) &&
          (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)) ? 1U : 0U;
}

static HAL_StatusTypeDef TouchGFX_DMA2D_Wait(uint8_t useInterrupt)
{
  uint32_t status;

  if (useInterrupt != 0U)
  {
    if (xSemaphoreTake(touchgfx_dma2d_semaphore,
                       pdMS_TO_TICKS(TOUCHGFX_DMA2D_IRQ_TIMEOUT_MS)) == pdTRUE)
    {
      __DMB();
      if (touchgfx_dma2d_custom_status != HAL_OK)
      {
        ++touchgfx_dma2d_error_count;
      }
      return touchgfx_dma2d_custom_status;
    }

    /* A hardware fault must not strand the GUI task. Mask the custom IRQ
       atomically, abort the engine and report the event through diagnostics. */
    taskENTER_CRITICAL();
    touchgfx_dma2d_custom_irq_active = 0U;
    DMA2D->CR &= ~(DMA2D_CR_TCIE | DMA2D_CR_TEIE | DMA2D_CR_CEIE);
    DMA2D->CR |= DMA2D_CR_ABORT;
    taskEXIT_CRITICAL();
    DMA2D->IFCR = DMA2D_IFCR_CTEIF | DMA2D_IFCR_CTCIF |
                  DMA2D_IFCR_CCEIF;
    ++touchgfx_dma2d_irq_timeout_count;
    ++touchgfx_dma2d_error_count;
    return HAL_TIMEOUT;
  }

  do
  {
    status = DMA2D->ISR;
  } while ((status & (DMA2D_FLAG_TC | DMA2D_FLAG_TE | DMA2D_FLAG_CE)) == 0U);

  if ((status & (DMA2D_FLAG_TE | DMA2D_FLAG_CE)) != 0U)
  {
    DMA2D->IFCR = DMA2D_IFCR_CTEIF | DMA2D_IFCR_CCEIF;
    ++touchgfx_dma2d_error_count;
    return HAL_ERROR;
  }

  DMA2D->IFCR = DMA2D_IFCR_CTCIF;
  return HAL_OK;
}

uint8_t TouchGFX_DMA2D_IRQHandler(void)
{
  uint32_t status;
  BaseType_t taskWoken = pdFALSE;

  if (touchgfx_dma2d_custom_irq_active == 0U)
  {
    return 0U;
  }

  status = DMA2D->ISR;
  if ((status & (DMA2D_FLAG_TC | DMA2D_FLAG_TE | DMA2D_FLAG_CE)) == 0U)
  {
    return 0U;
  }

  DMA2D->CR &= ~(DMA2D_CR_TCIE | DMA2D_CR_TEIE | DMA2D_CR_CEIE);
  DMA2D->IFCR = DMA2D_IFCR_CTEIF | DMA2D_IFCR_CTCIF |
                DMA2D_IFCR_CCEIF;
  touchgfx_dma2d_custom_status =
      ((status & (DMA2D_FLAG_TE | DMA2D_FLAG_CE)) == 0U) ? HAL_OK : HAL_ERROR;
  touchgfx_dma2d_custom_irq_active = 0U;
  __DMB();
  if (touchgfx_dma2d_semaphore != NULL)
  {
    (void)xSemaphoreGiveFromISR(touchgfx_dma2d_semaphore, &taskWoken);
  }
  portYIELD_FROM_ISR(taskWoken);
  return 1U;
}

HAL_StatusTypeDef TouchGFX_DMA2D_FillRGB888(uint32_t destination,
                                            uint16_t lineOffset,
                                            uint16_t width,
                                            uint16_t height,
                                            uint32_t color)
{
  HAL_StatusTypeDef status;
  uint32_t pixelCount;
  uint8_t useInterrupt;

  if ((width == 0U) || (height == 0U))
  {
    return HAL_OK;
  }

  /* Synchronize the client buffer before its first real write. In Video mode
     this performs only the front-to-back coherence copy; LTDC continuously
     scans the other buffer and there is no DSI refresh transaction to wait for. */
  TouchGFX_WaitForDisplayRefresh();

  pixelCount = (uint32_t)width * height;
  useInterrupt = TouchGFX_DMA2D_UseInterrupt(pixelCount);

  /* GUI drawing is serialized by the TouchGFX task. Polling is faster than
     taking an interrupt for the short solid-fill operations used here. */
  while ((DMA2D->CR & DMA2D_CR_START) != 0U)
  {
  }

  if (useInterrupt != 0U)
  {
    (void)xSemaphoreTake(touchgfx_dma2d_semaphore, 0U);
  }
  DMA2D->CR = DMA2D_R2M;
  DMA2D->OPFCCR = DMA2D_OUTPUT_RGB888;
  DMA2D->OCOLR = color & 0x00FFFFFFUL;
  DMA2D->OMAR = destination;
  DMA2D->OOR = lineOffset;
  DMA2D->NLR = ((uint32_t)width << 16U) | height;
  DMA2D->IFCR = DMA2D_IFCR_CTEIF | DMA2D_IFCR_CTCIF |
                DMA2D_IFCR_CTWIF | DMA2D_IFCR_CAECIF |
                DMA2D_IFCR_CCTCIF | DMA2D_IFCR_CCEIF;
  if (useInterrupt != 0U)
  {
    touchgfx_dma2d_custom_status = HAL_BUSY;
    touchgfx_dma2d_custom_irq_active = 1U;
    __DMB();
    DMA2D->CR |= DMA2D_CR_TCIE | DMA2D_CR_TEIE | DMA2D_CR_CEIE;
  }
  DMA2D->CR |= DMA2D_CR_START;
  status = TouchGFX_DMA2D_Wait(useInterrupt);
  if (status != HAL_OK) return status;
  if (useInterrupt != 0U) ++touchgfx_dma2d_irq_wait_count;
  ++touchgfx_dma2d_fill_count;
  touchgfx_dma2d_pixel_count += pixelCount;
  return HAL_OK;
}

HAL_StatusTypeDef TouchGFX_DMA2D_CopyRGB888(uint32_t source,
                                            uint32_t destination,
                                            uint16_t width,
                                            uint16_t height)
{
  return TouchGFX_DMA2D_CopyRGB888Rect(source, destination, 0U, 0U,
                                      width, height, width);
}

HAL_StatusTypeDef TouchGFX_DMA2D_CopyRGB888Rect(uint32_t source,
                                                uint32_t destination,
                                                uint16_t x,
                                                uint16_t y,
                                                uint16_t width,
                                                uint16_t height,
                                                uint16_t stridePixels)
{
  HAL_StatusTypeDef status;
  uint32_t pixelCount;
  uint32_t pixelOffset;
  uint16_t lineOffset;
  uint8_t useInterrupt;

  if ((width == 0U) || (height == 0U) || (source == destination))
  {
    return HAL_OK;
  }
  if (((uint32_t)x + width) > stridePixels)
  {
    ++touchgfx_dma2d_error_count;
    return HAL_ERROR;
  }

  pixelOffset = ((uint32_t)y * stridePixels) + x;
  lineOffset = (uint16_t)(stridePixels - width);
  pixelCount = (uint32_t)width * height;
  useInterrupt = TouchGFX_DMA2D_UseInterrupt(pixelCount);

  while ((DMA2D->CR & DMA2D_CR_START) != 0U)
  {
  }

  if (useInterrupt != 0U)
  {
    (void)xSemaphoreTake(touchgfx_dma2d_semaphore, 0U);
  }
  DMA2D->CR = DMA2D_M2M;
  DMA2D->FGPFCCR = DMA2D_INPUT_RGB888;
  DMA2D->FGMAR = source + (pixelOffset * 3U);
  DMA2D->FGOR = lineOffset;
  DMA2D->OPFCCR = DMA2D_OUTPUT_RGB888;
  DMA2D->OMAR = destination + (pixelOffset * 3U);
  DMA2D->OOR = lineOffset;
  DMA2D->NLR = ((uint32_t)width << 16U) | height;
  DMA2D->IFCR = DMA2D_IFCR_CTEIF | DMA2D_IFCR_CTCIF |
                DMA2D_IFCR_CTWIF | DMA2D_IFCR_CAECIF |
                DMA2D_IFCR_CCTCIF | DMA2D_IFCR_CCEIF;
  if (useInterrupt != 0U)
  {
    touchgfx_dma2d_custom_status = HAL_BUSY;
    touchgfx_dma2d_custom_irq_active = 1U;
    __DMB();
    DMA2D->CR |= DMA2D_CR_TCIE | DMA2D_CR_TEIE | DMA2D_CR_CEIE;
  }
  DMA2D->CR |= DMA2D_CR_START;
  status = TouchGFX_DMA2D_Wait(useInterrupt);
  if (status != HAL_OK) return status;
  if (useInterrupt != 0U) ++touchgfx_dma2d_irq_wait_count;
  ++touchgfx_dma2d_copy_count;
  touchgfx_dma2d_copy_pixel_count += pixelCount;
  return HAL_OK;
}

/* USER CODE END 1 */

