#include "usbd_storage.h"

#include "fatfs.h"
#include "sdmmc.h"
#include "usb_msc.h"

#define STORAGE_LUN_COUNT 1U

static int8_t STORAGE_Init(uint8_t lun);
static int8_t STORAGE_GetCapacity(uint8_t lun, uint32_t *block_num,
                                  uint16_t *block_size);
static int8_t STORAGE_IsReady(uint8_t lun);
static int8_t STORAGE_IsWriteProtected(uint8_t lun);
static int8_t STORAGE_Read(uint8_t lun, uint8_t *buf, uint32_t block,
                           uint16_t count);
static int8_t STORAGE_Write(uint8_t lun, uint8_t *buf, uint32_t block,
                            uint16_t count);
static int8_t STORAGE_GetMaxLun(void);

int8_t STORAGE_Inquirydata[] =
{
  0x00, 0x80, 0x02, 0x02, (STANDARD_INQUIRY_DATA_LEN - 5U), 0x00, 0x00, 0x00,
  'S','T','M','3','2','F','7',' ',
  'S','D',' ','C','a','r','d',' ','R','e','a','d','e','r',' ',' ',
  '1','.','0','0'
};

USBD_StorageTypeDef USBD_DISK_fops =
{
  STORAGE_Init,
  STORAGE_GetCapacity,
  STORAGE_IsReady,
  STORAGE_IsWriteProtected,
  STORAGE_Read,
  STORAGE_Write,
  STORAGE_GetMaxLun,
  STORAGE_Inquirydata
};

static uint8_t STORAGE_MediaAvailable(void)
{
  return ((SD_Storage_IsUsbOwned() != 0U) &&
          (SD_Card_IsPresent() != 0U)) ? 1U : 0U;
}

static int8_t STORAGE_Init(uint8_t lun)
{
  (void)lun;
  if (STORAGE_MediaAvailable() == 0U) return -1;
  if ((SD_Card_IsInitialized() == 0U) && (SD_Card_Init() != HAL_OK))
  {
    ++usb_msc_io_errors;
    return -1;
  }
  return 0;
}

static int8_t STORAGE_GetCapacity(uint8_t lun, uint32_t *block_num,
                                  uint16_t *block_size)
{
  HAL_SD_CardInfoTypeDef info;
  (void)lun;
  if ((block_num == NULL) || (block_size == NULL) ||
      (STORAGE_Init(0U) != 0) ||
      (HAL_SD_GetCardInfo(&hsd2, &info) != HAL_OK))
  {
    return -1;
  }
  *block_num = info.LogBlockNbr;
  *block_size = (uint16_t)info.LogBlockSize;
  return 0;
}

static int8_t STORAGE_IsReady(uint8_t lun)
{
  static uint8_t was_present;
  (void)lun;
  if (STORAGE_MediaAvailable() == 0U)
  {
    if (was_present != 0U)
    {
      (void)SD_Card_DeInit();
      was_present = 0U;
    }
    return -1;
  }
  if ((was_present == 0U) || (SD_Card_IsInitialized() == 0U))
  {
    (void)SD_Card_DeInit();
    if (SD_Card_Init() != HAL_OK) return -1;
    was_present = 1U;
  }
  return (HAL_SD_GetCardState(&hsd2) == HAL_SD_CARD_TRANSFER) ? 0 : -1;
}

static int8_t STORAGE_IsWriteProtected(uint8_t lun)
{
  (void)lun;
  return 0;
}

static int8_t STORAGE_WaitReady(void)
{
  uint32_t attempts = 1000000U;
  while (HAL_SD_GetCardState(&hsd2) != HAL_SD_CARD_TRANSFER)
  {
    if ((SD_Card_IsPresent() == 0U) || (--attempts == 0U)) return -1;
  }
  return 0;
}

static int8_t STORAGE_Read(uint8_t lun, uint8_t *buf, uint32_t block,
                           uint16_t count)
{
  (void)lun;
  if ((buf == NULL) || (count == 0U) || (STORAGE_IsReady(0U) != 0) ||
      (HAL_SD_ReadBlocks(&hsd2, buf, block, count, 1000U) != HAL_OK) ||
      (STORAGE_WaitReady() != 0))
  {
    ++usb_msc_io_errors;
    return -1;
  }
  usb_msc_read_blocks += count;
  return 0;
}

static int8_t STORAGE_Write(uint8_t lun, uint8_t *buf, uint32_t block,
                            uint16_t count)
{
  (void)lun;
  if ((buf == NULL) || (count == 0U) || (STORAGE_IsReady(0U) != 0) ||
      (HAL_SD_WriteBlocks(&hsd2, buf, block, count, 2000U) != HAL_OK) ||
      (STORAGE_WaitReady() != 0))
  {
    ++usb_msc_io_errors;
    return -1;
  }
  usb_msc_written_blocks += count;
  return 0;
}

static int8_t STORAGE_GetMaxLun(void)
{
  return (int8_t)(STORAGE_LUN_COUNT - 1U);
}
