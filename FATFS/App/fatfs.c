/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file   fatfs.c
  * @brief  Code for fatfs applications
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
#include "fatfs.h"

/* USER CODE BEGIN Includes */
#include "sdmmc.h"
#include "audio_recorder.h"
#include "audio_player.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
/* USER CODE END Includes */

uint8_t retUSER;    /* Return value for USER */
char USERPath[4];   /* USER logical drive path */
FATFS USERFatFS;    /* File system object for USER logical drive */
FIL USERFile;       /* File object for USER */

/* USER CODE BEGIN Variables */

volatile uint8_t sd_storage_status = SD_STORAGE_NO_CARD;
volatile uint32_t sd_storage_generation = 0U;
volatile uint32_t sd_mount_count = 0U;
volatile uint32_t sd_scan_count = 0U;
volatile uint32_t sd_mount_error_count = 0U;
volatile uint32_t sd_hot_remove_count = 0U;
volatile uint32_t sd_hot_insert_count = 0U;
volatile uint32_t sd_recovery_count = 0U;
static volatile uint8_t sd_remount_requested = 0U;
static volatile uint8_t sd_usb_export_requested = 0U;
static volatile uint8_t sd_usb_owned = 0U;

static SD_StorageSnapshot sd_storage_snapshot;

/* USER CODE END Variables */

void MX_FATFS_Init(void)
{
  /*## FatFS: Link the USER driver ###########################*/
  retUSER = FATFS_LinkDriver(&USER_Driver, USERPath);

  /* USER CODE BEGIN Init */
  /* additional user code for init */
  /* USER CODE END Init */
}

/**
  * @brief  Gets Time from RTC
  * @param  None
  * @retval Time in DWORD
  */
DWORD get_fattime(void)
{
  /* USER CODE BEGIN get_fattime */
  return 0;
  /* USER CODE END get_fattime */
}

/* USER CODE BEGIN Application */

static void SD_Storage_Publish(SD_StorageSnapshot *snapshot)
{
  taskENTER_CRITICAL();
  snapshot->generation = ++sd_storage_generation;
  sd_storage_status = (uint8_t)snapshot->status;
  sd_storage_snapshot = *snapshot;
  taskEXIT_CRITICAL();
}

static void SD_Storage_ResetVolume(uint8_t card_removed)
{
  /* Unregister the old filesystem object before invalidating the block device.
     This also makes a same-card software remount follow the exact hot-plug path. */
  (void)f_mount(NULL, USERPath, 0U);
  (void)FATFS_InvalidateDriver(USERPath);
  if (card_removed != 0U)
  {
    USER_SD_NotifyRemoved();
  }
  else
  {
    USER_SD_NotifyInserted();
  }
  (void)SD_Card_DeInit();
  memset(&USERFatFS, 0, sizeof(USERFatFS));
}

static FRESULT SD_Storage_Scan(SD_StorageSnapshot *snapshot)
{
  FATFS *filesystem;
  DIR directory;
  FILINFO info;
  DWORD free_clusters;
  FRESULT result;
  uint64_t total_sectors;
  uint64_t free_sectors;

  memset(snapshot, 0, sizeof(*snapshot));
  snapshot->status = SD_STORAGE_READY;

  result = f_getfree(USERPath, &free_clusters, &filesystem);
  if (result != FR_OK)
  {
    snapshot->last_result = (uint8_t)result;
    return result;
  }

  total_sectors = (uint64_t)(filesystem->n_fatent - 2U) * filesystem->csize;
  free_sectors = (uint64_t)free_clusters * filesystem->csize;
  snapshot->total_kb = (uint32_t)(total_sectors / 2U);
  snapshot->free_kb = (uint32_t)(free_sectors / 2U);

  result = f_opendir(&directory, USERPath);
  if (result != FR_OK)
  {
    snapshot->last_result = (uint8_t)result;
    return result;
  }

  for (;;)
  {
    result = f_readdir(&directory, &info);
    if ((result != FR_OK) || (info.fname[0] == 0))
    {
      break;
    }
    if ((info.fname[0] == '.') || ((info.fattrib & AM_HID) != 0U) ||
        ((info.fattrib & AM_SYS) != 0U))
    {
      continue;
    }

    ++snapshot->total_entry_count;
    if (snapshot->entry_count < SD_STORAGE_MAX_ENTRIES)
    {
      SD_StorageEntry *entry = &snapshot->entries[snapshot->entry_count++];
      strncpy(entry->name, info.fname, sizeof(entry->name) - 1U);
      entry->name[sizeof(entry->name) - 1U] = 0;
      entry->size = (uint32_t)info.fsize;
      entry->is_directory = ((info.fattrib & AM_DIR) != 0U) ? 1U : 0U;
    }
  }

  (void)f_closedir(&directory);
  snapshot->last_result = (uint8_t)result;
  if (result == FR_OK)
  {
    ++sd_scan_count;
  }
  return result;
}

void SD_Storage_GetSnapshot(SD_StorageSnapshot *snapshot)
{
  if (snapshot == NULL)
  {
    return;
  }
  taskENTER_CRITICAL();
  *snapshot = sd_storage_snapshot;
  taskEXIT_CRITICAL();
}

uint8_t SD_Storage_GetCachedEntryCount(void)
{
  uint8_t count;
  taskENTER_CRITICAL();
  count = sd_storage_snapshot.entry_count;
  taskEXIT_CRITICAL();
  return count;
}

void SD_Storage_RequestRemount(void)
{
  sd_remount_requested = 1U;
}

void SD_Storage_RequestUsbOwnership(uint8_t enable)
{
  sd_usb_export_requested = (enable != 0U) ? 1U : 0U;
  __DMB();
}

uint8_t SD_Storage_IsUsbOwned(void)
{
  return sd_usb_owned;
}

uint8_t SD_Storage_ApplicationAccessAllowed(void)
{
  return ((sd_usb_export_requested == 0U) && (sd_usb_owned == 0U)) ? 1U : 0U;
}

void SD_Storage_Task(void const *argument)
{
  uint8_t mounted = 0U;
  uint8_t stable_present = 0U;
  uint8_t candidate_present;
  uint8_t raw_present;
  uint32_t candidate_since;
  uint32_t next_scan_tick = 0U;
  SD_StorageStatus last_status = SD_STORAGE_ERROR;
  SD_StorageSnapshot snapshot;
  FRESULT result;

  (void)argument;
  memset(&sd_storage_snapshot, 0, sizeof(sd_storage_snapshot));
  candidate_present = SD_Card_IsPresent();
  candidate_since = HAL_GetTick();

  for (;;)
  {
    if (sd_usb_export_requested != sd_usb_owned)
    {
      if (sd_usb_export_requested != 0U)
      {
        /* Never remove a mounted volume from underneath an open WAV file. */
        if ((AudioRecorder_IsBusy() != 0U) || (AudioPlayer_IsBusy() != 0U))
        {
          vTaskDelay(pdMS_TO_TICKS(20U));
          continue;
        }
        (void)f_mount(NULL, USERPath, 0U);
        (void)FATFS_InvalidateDriver(USERPath);
        memset(&USERFatFS, 0, sizeof(USERFatFS));
        mounted = 0U;
        sd_usb_owned = 1U;
        memset(&snapshot, 0, sizeof(snapshot));
        snapshot.status = SD_STORAGE_USB_EXPORTED;
        SD_Storage_Publish(&snapshot);
        last_status = SD_STORAGE_USB_EXPORTED;
      }
      else
      {
        sd_usb_owned = 0U;
        mounted = 0U;
        SD_Storage_ResetVolume((SD_Card_IsPresent() == 0U) ? 1U : 0U);
        last_status = SD_STORAGE_ERROR;
      }
      vTaskDelay(pdMS_TO_TICKS(20U));
      continue;
    }

    if (sd_usb_owned != 0U)
    {
      /* USB MSC owns the raw blocks. FatFs must remain completely idle. */
      vTaskDelay(pdMS_TO_TICKS(20U));
      continue;
    }

    /* Recorder owns the volume while a WAV is open. Avoid directory scans or
       unmount operations racing the real-time writer. Card removal is first
       observed by the recorder, which closes the file before this task resumes. */
    if ((AudioRecorder_IsBusy() != 0U) || (AudioPlayer_IsBusy() != 0U))
    {
      vTaskDelay(pdMS_TO_TICKS(100U));
      continue;
    }

    raw_present = SD_Card_IsPresent();
    if (raw_present != candidate_present)
    {
      candidate_present = raw_present;
      candidate_since = HAL_GetTick();
    }

    /* PI15 is a mechanical switch.  Require 50 ms of stable state so contact
       bounce cannot start HAL_SD_Init while the card contacts are still moving. */
    if ((candidate_present != stable_present) &&
        ((HAL_GetTick() - candidate_since) >= 50U))
    {
      stable_present = candidate_present;
      mounted = 0U;
      if (stable_present == 0U)
      {
        ++sd_hot_remove_count;
        SD_Storage_ResetVolume(1U);
      }
      else
      {
        ++sd_hot_insert_count;
        SD_Storage_ResetVolume(0U);
      }
    }

    if (sd_remount_requested != 0U)
    {
      sd_remount_requested = 0U;
      mounted = 0U;
      ++sd_recovery_count;
      SD_Storage_ResetVolume((stable_present == 0U) ? 1U : 0U);
    }

    if (stable_present == 0U)
    {
      if (last_status != SD_STORAGE_NO_CARD)
      {
        memset(&snapshot, 0, sizeof(snapshot));
        snapshot.status = SD_STORAGE_NO_CARD;
        SD_Storage_Publish(&snapshot);
        last_status = SD_STORAGE_NO_CARD;
      }
      vTaskDelay(pdMS_TO_TICKS(20U));
      continue;
    }

    if (mounted == 0U)
    {
      memset(&snapshot, 0, sizeof(snapshot));
      snapshot.status = SD_STORAGE_MOUNTING;
      SD_Storage_Publish(&snapshot);
      last_status = SD_STORAGE_MOUNTING;

      result = f_mount(&USERFatFS, USERPath, 1U);
      if (result != FR_OK)
      {
        ++sd_mount_error_count;
        ++sd_recovery_count;
        SD_Storage_ResetVolume(0U);
        memset(&snapshot, 0, sizeof(snapshot));
        snapshot.status = (result == FR_NO_FILESYSTEM) ?
            SD_STORAGE_NO_FILESYSTEM : SD_STORAGE_ERROR;
        snapshot.last_result = (uint8_t)result;
        SD_Storage_Publish(&snapshot);
        last_status = snapshot.status;
        vTaskDelay(pdMS_TO_TICKS(250U));
        continue;
      }
      mounted = 1U;
      ++sd_mount_count;
      next_scan_tick = 0U;
    }

    /* Keep sampling the detect switch every 20 ms between directory scans.
       A single 1.5 s sleep could otherwise miss a quick remove/reinsert cycle. */
    if ((next_scan_tick != 0U) &&
        ((int32_t)(HAL_GetTick() - next_scan_tick) < 0))
    {
      vTaskDelay(pdMS_TO_TICKS(20U));
      continue;
    }

    result = SD_Storage_Scan(&snapshot);
    if (result == FR_OK)
    {
      SD_Storage_Publish(&snapshot);
      last_status = SD_STORAGE_READY;
      next_scan_tick = HAL_GetTick() + 1500U;
      vTaskDelay(pdMS_TO_TICKS(20U));
    }
    else
    {
      ++sd_mount_error_count;
      ++sd_recovery_count;
      SD_Storage_ResetVolume(0U);
      mounted = 0U;
      memset(&snapshot, 0, sizeof(snapshot));
      snapshot.status = (result == FR_NO_FILESYSTEM) ?
          SD_STORAGE_NO_FILESYSTEM : SD_STORAGE_ERROR;
      snapshot.last_result = (uint8_t)result;
      SD_Storage_Publish(&snapshot);
      last_status = snapshot.status;
      vTaskDelay(pdMS_TO_TICKS(250U));
    }
  }
}

/* USER CODE END Application */
