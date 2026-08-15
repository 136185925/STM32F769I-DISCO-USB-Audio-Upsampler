/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    sdmmc.h
  * @brief   This file contains all the function prototypes for
  *          the sdmmc.c file
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
#ifndef __SDMMC_H__
#define __SDMMC_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern SD_HandleTypeDef hsd2;
extern DMA_HandleTypeDef hdma_sdmmc2_rx;
extern DMA_HandleTypeDef hdma_sdmmc2_tx;
extern volatile uint32_t sd_controller_reset_count;
extern volatile uint32_t sd_controller_init_error_count;
extern volatile uint32_t sd_controller_last_error;

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

void MX_SDMMC2_SD_Init(void);

/* USER CODE BEGIN Prototypes */

HAL_StatusTypeDef SD_Card_Init(void);
HAL_StatusTypeDef SD_Card_DeInit(void);
uint8_t SD_Card_IsPresent(void);
uint8_t SD_Card_IsInitialized(void);
HAL_StatusTypeDef SD_DMA_Acquire(void);
void SD_DMA_Release(void);
uint8_t SD_DMA_IsClaimed(void);
void SD_DMA_BlockForDigitalAudio(void);
void SD_DMA_UnblockForDigitalAudio(void);

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __SDMMC_H__ */

