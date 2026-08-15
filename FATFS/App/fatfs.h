/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file   fatfs.h
  * @brief  Header for fatfs applications
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
#ifndef __fatfs_H
#define __fatfs_H
#ifdef __cplusplus
 extern "C" {
#endif

#include "ff.h"
#include "ff_gen_drv.h"
#include "user_diskio.h" /* defines USER_Driver as external */

/* USER CODE BEGIN Includes */

#include <stdint.h>

/* USER CODE END Includes */

extern uint8_t retUSER; /* Return value for USER */
extern char USERPath[4]; /* USER logical drive path */
extern FATFS USERFatFS; /* File system object for USER logical drive */
extern FIL USERFile; /* File object for USER */

void MX_FATFS_Init(void);

/* USER CODE BEGIN Prototypes */

#define SD_STORAGE_MAX_ENTRIES 64U

typedef enum
{
  SD_STORAGE_NO_CARD = 0,
  SD_STORAGE_MOUNTING,
  SD_STORAGE_READY,
  SD_STORAGE_NO_FILESYSTEM,
  SD_STORAGE_ERROR,
  SD_STORAGE_USB_EXPORTED
} SD_StorageStatus;

typedef struct
{
  char name[13];
  uint32_t size;
  uint8_t is_directory;
} SD_StorageEntry;

typedef struct
{
  SD_StorageStatus status;
  uint8_t entry_count;
  uint16_t total_entry_count;
  uint32_t generation;
  uint32_t total_kb;
  uint32_t free_kb;
  uint8_t last_result;
  SD_StorageEntry entries[SD_STORAGE_MAX_ENTRIES];
} SD_StorageSnapshot;

extern volatile uint8_t sd_storage_status;
extern volatile uint32_t sd_storage_generation;
extern volatile uint32_t sd_mount_count;
extern volatile uint32_t sd_scan_count;
extern volatile uint32_t sd_mount_error_count;
extern volatile uint32_t sd_hot_remove_count;
extern volatile uint32_t sd_hot_insert_count;
extern volatile uint32_t sd_recovery_count;

void SD_Storage_Task(void const *argument);
void SD_Storage_GetSnapshot(SD_StorageSnapshot *snapshot);
uint8_t SD_Storage_GetCachedEntryCount(void);
void SD_Storage_RequestRemount(void);
void SD_Storage_RequestUsbOwnership(uint8_t enable);
uint8_t SD_Storage_IsUsbOwned(void);
uint8_t SD_Storage_ApplicationAccessAllowed(void);

/* USER CODE END Prototypes */
#ifdef __cplusplus
}
#endif
#endif /*__fatfs_H */
