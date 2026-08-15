/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    sdmmc.c
  * @brief   This file provides code for the configuration
  *          of the SDMMC instances.
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
#include "sdmmc.h"

/* USER CODE BEGIN 0 */

#include <string.h>
#include "FreeRTOS.h"
#include "task.h"

/* USER CODE END 0 */

SD_HandleTypeDef hsd2;
DMA_HandleTypeDef hdma_sdmmc2_rx;
DMA_HandleTypeDef hdma_sdmmc2_tx;
static volatile uint8_t sd_card_initialized = 0U;
static volatile uint8_t sd_dma_claimed = 0U;
static volatile uint8_t sd_dma_blocked_by_audio = 0U;
volatile uint32_t sd_controller_reset_count = 0U;
volatile uint32_t sd_controller_init_error_count = 0U;
volatile uint32_t sd_controller_last_error = HAL_SD_ERROR_NONE;

/* SDMMC2 init function */

void MX_SDMMC2_SD_Init(void)
{

  /* USER CODE BEGIN SDMMC2_Init 0 */

  /* USER CODE END SDMMC2_Init 0 */

  /* USER CODE BEGIN SDMMC2_Init 1 */

  /* USER CODE END SDMMC2_Init 1 */
  hsd2.Instance = SDMMC2;
  hsd2.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
  hsd2.Init.ClockBypass = SDMMC_CLOCK_BYPASS_DISABLE;
  hsd2.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_ENABLE;
  hsd2.Init.BusWide = SDMMC_BUS_WIDE_1B;
  hsd2.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_ENABLE;
  hsd2.Init.ClockDiv = SDMMC_TRANSFER_CLK_DIV;
  if (HAL_SD_Init(&hsd2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_SD_ConfigWideBusOperation(&hsd2, SDMMC_BUS_WIDE_4B) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SDMMC2_Init 2 */

  /* USER CODE END SDMMC2_Init 2 */

}

void HAL_SD_MspInit(SD_HandleTypeDef* sdHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(sdHandle->Instance==SDMMC2)
  {
  /* USER CODE BEGIN SDMMC2_MspInit 0 */

  /* USER CODE END SDMMC2_MspInit 0 */
    /* SDMMC2 clock enable */
    __HAL_RCC_SDMMC2_CLK_ENABLE();

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    /**SDMMC2 GPIO Configuration
    PB4     ------> SDMMC2_D3
    PB3     ------> SDMMC2_D2
    PD7     ------> SDMMC2_CMD
    PD6     ------> SDMMC2_CK
    PG10     ------> SDMMC2_D1
    PG9     ------> SDMMC2_D0
    */
    GPIO_InitStruct.Pin = uSD_D3_Pin|uSD_D2_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF10_SDMMC2;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = uSD_CMD_Pin|uSD_CLK_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF11_SDMMC2;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = uSD_D1_Pin|uSD_D0_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF11_SDMMC2;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

  /* USER CODE BEGIN SDMMC2_MspInit 1 */

  /* USER CODE END SDMMC2_MspInit 1 */
  }
}

void HAL_SD_MspDeInit(SD_HandleTypeDef* sdHandle)
{

  if(sdHandle->Instance==SDMMC2)
  {
  /* USER CODE BEGIN SDMMC2_MspDeInit 0 */

  /* USER CODE END SDMMC2_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_SDMMC2_CLK_DISABLE();

    /**SDMMC2 GPIO Configuration
    PB4     ------> SDMMC2_D3
    PB3     ------> SDMMC2_D2
    PD7     ------> SDMMC2_CMD
    PD6     ------> SDMMC2_CK
    PG10     ------> SDMMC2_D1
    PG9     ------> SDMMC2_D0
    */
    HAL_GPIO_DeInit(GPIOB, uSD_D3_Pin|uSD_D2_Pin);

    HAL_GPIO_DeInit(GPIOD, uSD_CMD_Pin|uSD_CLK_Pin);

    HAL_GPIO_DeInit(GPIOG, uSD_D1_Pin|uSD_D0_Pin);

  /* USER CODE BEGIN SDMMC2_MspDeInit 1 */

  /* USER CODE END SDMMC2_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

static uint32_t SD_DMA_EnterAtomic(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  __DMB();
  return primask;
}

static void SD_DMA_ExitAtomic(uint32_t primask)
{
  __DMB();
  if (primask == 0U)
  {
    __enable_irq();
  }
}

uint8_t SD_DMA_IsClaimed(void)
{
  return sd_dma_claimed;
}

HAL_StatusTypeDef SD_DMA_Acquire(void)
{
  HAL_StatusTypeDef status;
  uint32_t primask;

  /* The completion path uses FreeRTOS synchronization. Calls made before the
     scheduler starts retain the safe polling disk path. */
  if ((__get_IPSR() != 0U) ||
      (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING))
  {
    return HAL_BUSY;
  }

  primask = SD_DMA_EnterAtomic();
  if ((sd_dma_blocked_by_audio != 0U) || (sd_dma_claimed != 0U))
  {
    SD_DMA_ExitAtomic(primask);
    return HAL_BUSY;
  }
  sd_dma_claimed = 1U;
  SD_DMA_ExitAtomic(primask);

  __HAL_RCC_DMA2_CLK_ENABLE();

  memset(&hdma_sdmmc2_rx, 0, sizeof(hdma_sdmmc2_rx));
  hdma_sdmmc2_rx.Instance = DMA2_Stream0;
  hdma_sdmmc2_rx.Init.Channel = DMA_CHANNEL_11;
  hdma_sdmmc2_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
  hdma_sdmmc2_rx.Init.PeriphInc = DMA_PINC_DISABLE;
  hdma_sdmmc2_rx.Init.MemInc = DMA_MINC_ENABLE;
  hdma_sdmmc2_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
  hdma_sdmmc2_rx.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
  hdma_sdmmc2_rx.Init.Mode = DMA_PFCTRL;
  hdma_sdmmc2_rx.Init.Priority = DMA_PRIORITY_VERY_HIGH;
  hdma_sdmmc2_rx.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
  hdma_sdmmc2_rx.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
  hdma_sdmmc2_rx.Init.MemBurst = DMA_MBURST_INC4;
  hdma_sdmmc2_rx.Init.PeriphBurst = DMA_PBURST_INC4;

  memset(&hdma_sdmmc2_tx, 0, sizeof(hdma_sdmmc2_tx));
  hdma_sdmmc2_tx.Instance = DMA2_Stream5;
  hdma_sdmmc2_tx.Init.Channel = DMA_CHANNEL_11;
  hdma_sdmmc2_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
  hdma_sdmmc2_tx.Init.PeriphInc = DMA_PINC_DISABLE;
  hdma_sdmmc2_tx.Init.MemInc = DMA_MINC_ENABLE;
  hdma_sdmmc2_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
  hdma_sdmmc2_tx.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
  hdma_sdmmc2_tx.Init.Mode = DMA_PFCTRL;
  hdma_sdmmc2_tx.Init.Priority = DMA_PRIORITY_VERY_HIGH;
  hdma_sdmmc2_tx.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
  hdma_sdmmc2_tx.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
  hdma_sdmmc2_tx.Init.MemBurst = DMA_MBURST_INC4;
  hdma_sdmmc2_tx.Init.PeriphBurst = DMA_PBURST_INC4;

  __HAL_LINKDMA(&hsd2, hdmarx, hdma_sdmmc2_rx);
  __HAL_LINKDMA(&hsd2, hdmatx, hdma_sdmmc2_tx);
  status = HAL_DMA_Init(&hdma_sdmmc2_rx);
  if (status == HAL_OK)
  {
    status = HAL_DMA_Init(&hdma_sdmmc2_tx);
  }
  if (status != HAL_OK)
  {
    SD_DMA_Release();
    return HAL_ERROR;
  }

  /* Both IRQs call FreeRTOS FromISR APIs, so keep them at or below the
     configured max-syscall priority (5). */
  HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 6U, 0U);
  HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
  HAL_NVIC_SetPriority(DMA2_Stream5_IRQn, 6U, 0U);
  HAL_NVIC_EnableIRQ(DMA2_Stream5_IRQn);
  HAL_NVIC_SetPriority(SDMMC2_IRQn, 6U, 0U);
  HAL_NVIC_EnableIRQ(SDMMC2_IRQn);
  return HAL_OK;
}

void SD_DMA_Release(void)
{
  uint32_t primask;

  HAL_NVIC_DisableIRQ(SDMMC2_IRQn);
  HAL_NVIC_DisableIRQ(DMA2_Stream0_IRQn);
  HAL_NVIC_DisableIRQ(DMA2_Stream5_IRQn);
  (void)HAL_DMA_DeInit(&hdma_sdmmc2_rx);
  (void)HAL_DMA_DeInit(&hdma_sdmmc2_tx);
  hsd2.hdmarx = NULL;
  hsd2.hdmatx = NULL;

  primask = SD_DMA_EnterAtomic();
  sd_dma_claimed = 0U;
  SD_DMA_ExitAtomic(primask);
}

void SD_DMA_BlockForDigitalAudio(void)
{
  uint32_t primask;

  /* Claim the two shared streams for DFSDM before its MSP rewrites them. If
     an SD chunk is already in flight, wait for that bounded transfer only. */
  primask = SD_DMA_EnterAtomic();
  sd_dma_blocked_by_audio = 1U;
  SD_DMA_ExitAtomic(primask);

  while (sd_dma_claimed != 0U)
  {
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
      vTaskDelay(pdMS_TO_TICKS(1U));
    }
    else
    {
      __NOP();
    }
  }
}

void SD_DMA_UnblockForDigitalAudio(void)
{
  uint32_t primask = SD_DMA_EnterAtomic();
  sd_dma_blocked_by_audio = 0U;
  SD_DMA_ExitAtomic(primask);
}

uint8_t SD_Card_IsPresent(void)
{
  /* The socket switch on PI15 is active low. */
  return (HAL_GPIO_ReadPin(uSD_Detect_GPIO_Port, uSD_Detect_Pin) == GPIO_PIN_RESET) ? 1U : 0U;
}

uint8_t SD_Card_IsInitialized(void)
{
  return sd_card_initialized;
}

HAL_StatusTypeDef SD_Card_Init(void)
{
  if (SD_Card_IsPresent() == 0U)
  {
    sd_card_initialized = 0U;
    return HAL_ERROR;
  }

  /* A removed card resets internally even if the detect switch transition was
     too short for the storage task to observe.  Always start a new FatFs mount
     from a reset SDMMC instance instead of reusing the old HAL state. */
  if ((sd_card_initialized == 0U) &&
      ((hsd2.Instance != SDMMC2) || (hsd2.State != HAL_SD_STATE_RESET)))
  {
    (void)SD_Card_DeInit();
  }

  hsd2.Instance = SDMMC2;
  hsd2.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
  hsd2.Init.ClockBypass = SDMMC_CLOCK_BYPASS_DISABLE;
  hsd2.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_ENABLE;
  hsd2.Init.BusWide = SDMMC_BUS_WIDE_1B;
  hsd2.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_ENABLE;
  hsd2.Init.ClockDiv = SDMMC_TRANSFER_CLK_DIV;
  if (HAL_SD_Init(&hsd2) != HAL_OK)
  {
    sd_controller_last_error = hsd2.ErrorCode;
    ++sd_controller_init_error_count;
    (void)SD_Card_DeInit();
    return HAL_ERROR;
  }
  if (HAL_SD_ConfigWideBusOperation(&hsd2, SDMMC_BUS_WIDE_4B) != HAL_OK)
  {
    sd_controller_last_error = hsd2.ErrorCode;
    ++sd_controller_init_error_count;
    (void)SD_Card_DeInit();
    return HAL_ERROR;
  }
  sd_controller_last_error = HAL_SD_ERROR_NONE;
  sd_card_initialized = 1U;
  return HAL_OK;
}

HAL_StatusTypeDef SD_Card_DeInit(void)
{
  HAL_StatusTypeDef status = HAL_OK;

  /* Do not key de-initialization only from sd_card_initialized.  HAL_SD_Init()
     can fail after MSP init and leave the handle BUSY/ERROR while that flag is
     still zero.  That stale state is the usual reason a reinsert needs reset. */
  if ((hsd2.Instance == SDMMC2) && (hsd2.State != HAL_SD_STATE_RESET))
  {
    status = HAL_SD_DeInit(&hsd2);
  }

  /* HAL de-init powers off the card clock and releases the pins.  The RCC reset
     additionally clears command/data-path flags that can survive an abrupt
     removal in the middle of a transfer. */
  __HAL_RCC_SDMMC2_FORCE_RESET();
  __DSB();
  __HAL_RCC_SDMMC2_RELEASE_RESET();
  __HAL_RCC_SDMMC2_CLK_DISABLE();

  memset(&hsd2, 0, sizeof(hsd2));
  hsd2.Instance = SDMMC2;
  hsd2.State = HAL_SD_STATE_RESET;
  hsd2.Lock = HAL_UNLOCKED;
  sd_card_initialized = 0U;
  ++sd_controller_reset_count;
  return status;
}

/* USER CODE END 1 */

