/* USER CODE BEGIN Header */
/**
 ******************************************************************************
  * @file    user_diskio.c
  * @brief   This file includes a diskio driver skeleton to be completed by the user.
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

#ifdef USE_OBSOLETE_USER_CODE_SECTION_0
/*
 * Warning: the user section 0 is no more in use (starting from CubeMx version 4.16.0)
 * To be suppressed in the future.
 * Kept to ensure backward compatibility with previous CubeMx versions when
 * migrating projects.
 * User code previously added there should be copied in the new user sections before
 * the section contents can be deleted.
 */
/* USER CODE BEGIN 0 */
/* USER CODE END 0 */
#endif

/* USER CODE BEGIN DECL */

/* Includes ------------------------------------------------------------------*/
#include <string.h>
#include "ff_gen_drv.h"
#include "sdmmc.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
/* Disk status */
static volatile DSTATUS Stat = STA_NOINIT;
volatile uint32_t sd_disk_read_count = 0U;
volatile uint32_t sd_disk_write_count = 0U;
volatile uint32_t sd_disk_error_count = 0U;
volatile uint32_t sd_disk_dma_read_count = 0U;
volatile uint32_t sd_disk_dma_write_count = 0U;
volatile uint32_t sd_disk_dma_fallback_count = 0U;
volatile uint32_t sd_disk_dma_timeout_count = 0U;

#define USER_SD_DMA_SECTORS_PER_CHUNK  8U
#define USER_SD_DMA_TIMEOUT_MS         2000U

/* The MPU maps .dma_buffer as non-cacheable. A bounded bounce buffer avoids
   cache-line invalidation hazards for arbitrary FatFs buffers and also keeps
   every SD DMA address word-aligned. */
__attribute__((section(".dma_buffer"), aligned(32)))
static uint8_t sd_dma_bounce[USER_SD_DMA_SECTORS_PER_CHUNK * 512U];
static StaticSemaphore_t sd_dma_semaphore_control;
static SemaphoreHandle_t sd_dma_semaphore;
static volatile HAL_StatusTypeDef sd_dma_result = HAL_ERROR;
static HAL_StatusTypeDef USER_wait_ready(uint32_t timeout);

static void USER_dma_signal_from_isr(HAL_StatusTypeDef result)
{
  BaseType_t task_woken = pdFALSE;

  if ((SD_DMA_IsClaimed() == 0U) || (sd_dma_semaphore == NULL))
  {
    return;
  }
  sd_dma_result = result;
  __DMB();
  (void)xSemaphoreGiveFromISR(sd_dma_semaphore, &task_woken);
  portYIELD_FROM_ISR(task_woken);
}

void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd)
{
  if (hsd == &hsd2)
  {
    USER_dma_signal_from_isr(HAL_OK);
  }
}

void HAL_SD_TxCpltCallback(SD_HandleTypeDef *hsd)
{
  if (hsd == &hsd2)
  {
    USER_dma_signal_from_isr(HAL_OK);
  }
}

void HAL_SD_ErrorCallback(SD_HandleTypeDef *hsd)
{
  if (hsd == &hsd2)
  {
    USER_dma_signal_from_isr(HAL_ERROR);
  }
}

void HAL_SD_AbortCallback(SD_HandleTypeDef *hsd)
{
  if (hsd == &hsd2)
  {
    USER_dma_signal_from_isr(HAL_ERROR);
  }
}

static void USER_dma_create_resources(void)
{
  if (sd_dma_semaphore == NULL)
  {
    sd_dma_semaphore = xSemaphoreCreateBinaryStatic(&sd_dma_semaphore_control);
  }
}

static HAL_StatusTypeDef USER_dma_wait(void)
{
  if ((sd_dma_semaphore == NULL) ||
      (xSemaphoreTake(sd_dma_semaphore,
                      pdMS_TO_TICKS(USER_SD_DMA_TIMEOUT_MS)) != pdTRUE))
  {
    ++sd_disk_dma_timeout_count;
    (void)HAL_SD_Abort(&hsd2);
    return HAL_TIMEOUT;
  }
  __DMB();
  return sd_dma_result;
}

static HAL_StatusTypeDef USER_read_dma(BYTE *buff, DWORD sector, UINT count)
{
  HAL_StatusTypeDef status;

  USER_dma_create_resources();
  status = SD_DMA_Acquire();
  if (status != HAL_OK)
  {
    ++sd_disk_dma_fallback_count;
    return HAL_BUSY;
  }

  while (count != 0U)
  {
    UINT chunk = (count > USER_SD_DMA_SECTORS_PER_CHUNK) ?
                 USER_SD_DMA_SECTORS_PER_CHUNK : count;
    (void)xSemaphoreTake(sd_dma_semaphore, 0U);
    sd_dma_result = HAL_BUSY;
    __DMB();
    status = HAL_SD_ReadBlocks_DMA(&hsd2, sd_dma_bounce, sector, chunk);
    if (status == HAL_OK)
    {
      status = USER_dma_wait();
    }
    if ((status == HAL_OK) && (USER_wait_ready(1000U) == HAL_OK))
    {
      memcpy(buff, sd_dma_bounce, chunk * 512U);
      buff += chunk * 512U;
      sector += chunk;
      count -= chunk;
      sd_disk_dma_read_count += chunk;
    }
    else
    {
      status = HAL_ERROR;
      break;
    }
  }

  SD_DMA_Release();
  return status;
}

static HAL_StatusTypeDef USER_write_dma(const BYTE *buff, DWORD sector, UINT count)
{
  HAL_StatusTypeDef status;

  USER_dma_create_resources();
  status = SD_DMA_Acquire();
  if (status != HAL_OK)
  {
    ++sd_disk_dma_fallback_count;
    return HAL_BUSY;
  }

  while (count != 0U)
  {
    UINT chunk = (count > USER_SD_DMA_SECTORS_PER_CHUNK) ?
                 USER_SD_DMA_SECTORS_PER_CHUNK : count;
    memcpy(sd_dma_bounce, buff, chunk * 512U);
    __DMB();
    (void)xSemaphoreTake(sd_dma_semaphore, 0U);
    sd_dma_result = HAL_BUSY;
    status = HAL_SD_WriteBlocks_DMA(&hsd2, sd_dma_bounce, sector, chunk);
    if (status == HAL_OK)
    {
      status = USER_dma_wait();
    }
    if ((status == HAL_OK) && (USER_wait_ready(2000U) == HAL_OK))
    {
      buff += chunk * 512U;
      sector += chunk;
      count -= chunk;
      sd_disk_dma_write_count += chunk;
    }
    else
    {
      status = HAL_ERROR;
      break;
    }
  }

  SD_DMA_Release();
  return status;
}

void USER_SD_NotifyRemoved(void)
{
  Stat = STA_NOINIT | STA_NODISK;
  __DMB();
}

void USER_SD_NotifyInserted(void)
{
  /* Force FatFs to call disk_initialize() on the next immediate mount. */
  Stat = STA_NOINIT;
  __DMB();
}

static HAL_StatusTypeDef USER_wait_ready(uint32_t timeout)
{
  uint32_t start = HAL_GetTick();
  while (HAL_SD_GetCardState(&hsd2) != HAL_SD_CARD_TRANSFER)
  {
    if ((SD_Card_IsPresent() == 0U) || ((HAL_GetTick() - start) >= timeout))
    {
      return HAL_TIMEOUT;
    }
    osDelay(1U);
  }
  return HAL_OK;
}

/* USER CODE END DECL */

/* Private function prototypes -----------------------------------------------*/
DSTATUS USER_initialize (BYTE pdrv);
DSTATUS USER_status (BYTE pdrv);
DRESULT USER_read (BYTE pdrv, BYTE *buff, DWORD sector, UINT count);
#if _USE_WRITE == 1
  DRESULT USER_write (BYTE pdrv, const BYTE *buff, DWORD sector, UINT count);
#endif /* _USE_WRITE == 1 */
#if _USE_IOCTL == 1
  DRESULT USER_ioctl (BYTE pdrv, BYTE cmd, void *buff);
#endif /* _USE_IOCTL == 1 */

Diskio_drvTypeDef  USER_Driver =
{
  USER_initialize,
  USER_status,
  USER_read,
#if  _USE_WRITE
  USER_write,
#endif  /* _USE_WRITE == 1 */
#if  _USE_IOCTL == 1
  USER_ioctl,
#endif /* _USE_IOCTL == 1 */
};

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Initializes a Drive
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS USER_initialize (
	BYTE pdrv           /* Physical drive nmuber to identify the drive */
)
{
  /* USER CODE BEGIN INIT */
    if (pdrv != 0U)
    {
      return STA_NOINIT;
    }
    if (SD_Card_IsPresent() == 0U)
    {
      Stat = STA_NOINIT | STA_NODISK;
      return Stat;
    }
    if ((SD_Card_IsInitialized() == 0U) && (SD_Card_Init() != HAL_OK))
    {
      ++sd_disk_error_count;
      Stat = STA_NOINIT;
      return Stat;
    }
    Stat = 0U;
    return Stat;
  /* USER CODE END INIT */
}

/**
  * @brief  Gets Disk Status
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS USER_status (
	BYTE pdrv       /* Physical drive number to identify the drive */
)
{
  /* USER CODE BEGIN STATUS */
    if ((pdrv != 0U) || (SD_Card_IsPresent() == 0U))
    {
      Stat = STA_NOINIT | STA_NODISK;
    }
    else if (SD_Card_IsInitialized() == 0U)
    {
      Stat = STA_NOINIT;
    }
    else
    {
      Stat = 0U;
    }
    return Stat;
  /* USER CODE END STATUS */
}

/**
  * @brief  Reads Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data buffer to store read data
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to read (1..128)
  * @retval DRESULT: Operation result
  */
DRESULT USER_read (
	BYTE pdrv,      /* Physical drive nmuber to identify the drive */
	BYTE *buff,     /* Data buffer to store read data */
	DWORD sector,   /* Sector address in LBA */
	UINT count      /* Number of sectors to read */
)
{
  /* USER CODE BEGIN READ */
    if ((pdrv != 0U) || (buff == NULL) || (count == 0U))
    {
      return RES_PARERR;
    }
    if (USER_status(pdrv) != 0U)
    {
      return RES_NOTRDY;
    }
    HAL_StatusTypeDef status = USER_read_dma(buff, sector, count);
    if (status == HAL_BUSY)
    {
      status = HAL_SD_ReadBlocks(&hsd2, buff, sector, count, 1000U);
      if ((status == HAL_OK) && (USER_wait_ready(1000U) != HAL_OK))
      {
        status = HAL_ERROR;
      }
    }
    if (status != HAL_OK)
    {
      ++sd_disk_error_count;
      return RES_ERROR;
    }
    sd_disk_read_count += count;
    return RES_OK;
  /* USER CODE END READ */
}

/**
  * @brief  Writes Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data to be written
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to write (1..128)
  * @retval DRESULT: Operation result
  */
#if _USE_WRITE == 1
DRESULT USER_write (
	BYTE pdrv,          /* Physical drive nmuber to identify the drive */
	const BYTE *buff,   /* Data to be written */
	DWORD sector,       /* Sector address in LBA */
	UINT count          /* Number of sectors to write */
)
{
  /* USER CODE BEGIN WRITE */
    if ((pdrv != 0U) || (buff == NULL) || (count == 0U))
    {
      return RES_PARERR;
    }
    if (USER_status(pdrv) != 0U)
    {
      return RES_NOTRDY;
    }
    HAL_StatusTypeDef status = USER_write_dma(buff, sector, count);
    if (status == HAL_BUSY)
    {
      status = HAL_SD_WriteBlocks(&hsd2, (uint8_t*)buff, sector, count, 1000U);
      if ((status == HAL_OK) && (USER_wait_ready(2000U) != HAL_OK))
      {
        status = HAL_ERROR;
      }
    }
    if (status != HAL_OK)
    {
      ++sd_disk_error_count;
      return RES_ERROR;
    }
    sd_disk_write_count += count;
    return RES_OK;
  /* USER CODE END WRITE */
}
#endif /* _USE_WRITE == 1 */

/**
  * @brief  I/O control operation
  * @param  pdrv: Physical drive number (0..)
  * @param  cmd: Control code
  * @param  *buff: Buffer to send/receive control data
  * @retval DRESULT: Operation result
  */
#if _USE_IOCTL == 1
DRESULT USER_ioctl (
	BYTE pdrv,      /* Physical drive nmuber (0..) */
	BYTE cmd,       /* Control code */
	void *buff      /* Buffer to send/receive control data */
)
{
  /* USER CODE BEGIN IOCTL */
    HAL_SD_CardInfoTypeDef info;
    if (pdrv != 0U)
    {
      return RES_PARERR;
    }
    if (USER_status(pdrv) != 0U)
    {
      return RES_NOTRDY;
    }
    switch (cmd)
    {
      case CTRL_SYNC:
        return (USER_wait_ready(1000U) == HAL_OK) ? RES_OK : RES_ERROR;
      case GET_SECTOR_COUNT:
        if ((buff != NULL) && (HAL_SD_GetCardInfo(&hsd2, &info) == HAL_OK))
        {
          *(DWORD*)buff = info.LogBlockNbr;
          return RES_OK;
        }
        break;
      case GET_SECTOR_SIZE:
        if (buff != NULL)
        {
          *(WORD*)buff = 512U;
          return RES_OK;
        }
        break;
      case GET_BLOCK_SIZE:
        if (buff != NULL)
        {
          *(DWORD*)buff = 1U;
          return RES_OK;
        }
        break;
      default:
        return RES_PARERR;
    }
    ++sd_disk_error_count;
    return RES_ERROR;
  /* USER CODE END IOCTL */
}
#endif /* _USE_IOCTL == 1 */

