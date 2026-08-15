#include "usb_msc.h"

#include "audio_player.h"
#include "audio_recorder.h"
#include "audio_spectrum.h"
#include "fatfs.h"
#include "sdmmc.h"
#include "usb_audio.h"
#include "usb_otg.h"
#include "usbd_audio.h"
#include "usbd_audio_if.h"
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_msc.h"
#include "usbd_storage.h"
#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

USBD_HandleTypeDef hUsbDeviceHS;

volatile uint32_t usb_msc_read_blocks = 0U;
volatile uint32_t usb_msc_written_blocks = 0U;
volatile uint32_t usb_msc_io_errors = 0U;

typedef enum
{
  USB_DEVICE_MODE_OFF = 0,
  USB_DEVICE_MODE_MSC,
  USB_DEVICE_MODE_AUDIO
} USB_DeviceMode;

static TaskHandle_t usb_msc_task_handle;
static volatile uint8_t usb_msc_requested;
static volatile uint8_t usb_msc_active;
static USB_MSC_Snapshot usb_msc_snapshot;

static uint8_t USB_Device_LinkIsHighSpeed(void)
{
  USB_OTG_DeviceTypeDef *device;
  if ((hUsbDeviceHS.dev_state != USBD_STATE_CONFIGURED) ||
      (hpcd_USB_OTG_HS.Instance == NULL))
  {
    return 0U;
  }
  device = (USB_OTG_DeviceTypeDef *)
      ((uintptr_t)hpcd_USB_OTG_HS.Instance + USB_OTG_DEVICE_BASE);
  return ((device->DSTS & USB_OTG_DSTS_ENUMSPD_Msk) ==
          DSTS_ENUMSPD_HS_PHY_30MHZ_OR_60MHZ) ? 1U : 0U;
}

static void USB_MSC_Publish(USB_MSC_Status status)
{
  const uint8_t configured =
      ((usb_msc_active != 0U) &&
       (hUsbDeviceHS.dev_state == USBD_STATE_CONFIGURED)) ? 1U : 0U;
  const uint8_t card_present = SD_Card_IsPresent();
  const uint8_t high_speed =
      (usb_msc_active != 0U) ? USB_Device_LinkIsHighSpeed() : 0U;

  taskENTER_CRITICAL();
  const uint8_t changed =
      ((usb_msc_snapshot.status != status) ||
       (usb_msc_snapshot.requested != usb_msc_requested) ||
       (usb_msc_snapshot.active != usb_msc_active) ||
       (usb_msc_snapshot.configured != configured) ||
       (usb_msc_snapshot.card_present != card_present) ||
       (usb_msc_snapshot.high_speed != high_speed) ||
       (usb_msc_snapshot.read_blocks != usb_msc_read_blocks) ||
       (usb_msc_snapshot.written_blocks != usb_msc_written_blocks) ||
       (usb_msc_snapshot.io_errors != usb_msc_io_errors)) ? 1U : 0U;

  usb_msc_snapshot.status = status;
  usb_msc_snapshot.requested = usb_msc_requested;
  usb_msc_snapshot.active = usb_msc_active;
  usb_msc_snapshot.configured = configured;
  usb_msc_snapshot.card_present = card_present;
  usb_msc_snapshot.high_speed = high_speed;
  usb_msc_snapshot.read_blocks = usb_msc_read_blocks;
  usb_msc_snapshot.written_blocks = usb_msc_written_blocks;
  usb_msc_snapshot.io_errors = usb_msc_io_errors;
  if (changed != 0U) ++usb_msc_snapshot.generation;
  taskEXIT_CRITICAL();
}

void USB_MSC_CreateResources(void)
{
  memset(&usb_msc_snapshot, 0, sizeof(usb_msc_snapshot));
  usb_msc_snapshot.status = USB_MSC_OFF;
}

void USB_MSC_GetSnapshot(USB_MSC_Snapshot *snapshot)
{
  if (snapshot == NULL) return;
  taskENTER_CRITICAL();
  *snapshot = usb_msc_snapshot;
  taskEXIT_CRITICAL();
}

uint8_t USB_MSC_IsActive(void)
{
  return usb_msc_active;
}

void USB_MSC_WakeManager(void)
{
  if (usb_msc_task_handle != NULL) (void)xTaskNotifyGive(usb_msc_task_handle);
}

void USB_MSC_RequestEnable(uint8_t enable)
{
  usb_msc_requested = (enable != 0U) ? 1U : 0U;
  if (enable != 0U) USB_Audio_RequestEnable(0U);
  USB_MSC_WakeManager();
}

static uint8_t USB_Device_Start(USB_DeviceMode mode,
                                USB_AudioHostMode audio_host_mode)
{
  if (mode == USB_DEVICE_MODE_AUDIO)
  {
    USBD_AUDIO_SetProtocol(
        (audio_host_mode == USB_AUDIO_HOST_WINDOWS) ?
        USBD_AUDIO_PROTOCOL_UAC2 : USBD_AUDIO_PROTOCOL_UAC1);
  }
  memset(&hUsbDeviceHS, 0, sizeof(hUsbDeviceHS));
  if (USBD_Init(&hUsbDeviceHS,
                (mode == USB_DEVICE_MODE_AUDIO) ? &AUDIO_Desc : &MSC_Desc,
                0U) != USBD_OK) return 0U;
  if (USBD_RegisterClass(&hUsbDeviceHS,
                         (mode == USB_DEVICE_MODE_AUDIO) ?
                         USBD_AUDIO_CLASS : USBD_MSC_CLASS) != USBD_OK)
  {
    (void)USBD_DeInit(&hUsbDeviceHS);
    return 0U;
  }
  if (mode == USB_DEVICE_MODE_AUDIO)
  {
    if (USBD_AUDIO_RegisterInterface(&hUsbDeviceHS, &USBD_AUDIO_fops) != USBD_OK)
    {
      (void)USBD_DeInit(&hUsbDeviceHS);
      return 0U;
    }
  }
  else if (USBD_MSC_RegisterStorage(&hUsbDeviceHS, &USBD_DISK_fops) != USBD_OK)
  {
    (void)USBD_DeInit(&hUsbDeviceHS);
    return 0U;
  }
  if (USBD_Start(&hUsbDeviceHS) != USBD_OK)
  {
    (void)USBD_DeInit(&hUsbDeviceHS);
    return 0U;
  }
  return 1U;
}

static void USB_Device_Stop(void)
{
  (void)USBD_Stop(&hUsbDeviceHS);
  (void)USBD_DeInit(&hUsbDeviceHS);
  memset(&hUsbDeviceHS, 0, sizeof(hUsbDeviceHS));
}

void USB_MSC_Task(void const *argument)
{
  USB_MSC_Status msc_status = USB_MSC_OFF;
  USB_DeviceMode active_mode = USB_DEVICE_MODE_OFF;
  USB_AudioHostMode active_audio_host_mode = USB_AUDIO_HOST_LINUX;
  (void)argument;
  usb_msc_task_handle = xTaskGetCurrentTaskHandle();
  USB_MSC_Publish(USB_MSC_OFF);

  for (;;)
  {
    USB_DeviceMode target_mode = USB_DEVICE_MODE_OFF;
    const USB_AudioHostMode target_audio_host_mode = USB_Audio_GetHostMode();
    if (USB_Audio_EnableRequested() != 0U) target_mode = USB_DEVICE_MODE_AUDIO;
    else if (usb_msc_requested != 0U) target_mode = USB_DEVICE_MODE_MSC;

    if ((target_mode != active_mode) ||
        ((active_mode == USB_DEVICE_MODE_AUDIO) &&
         (active_audio_host_mode != target_audio_host_mode)))
    {
      if (active_mode == USB_DEVICE_MODE_MSC)
      {
        msc_status = USB_MSC_STOPPING;
        USB_MSC_Publish(msc_status);
        USB_Device_Stop();
        usb_msc_active = 0U;
        SD_Storage_RequestUsbOwnership(0U);
        active_mode = USB_DEVICE_MODE_OFF;
        msc_status = USB_MSC_OFF;
      }
      else if (active_mode == USB_DEVICE_MODE_AUDIO)
      {
        USB_Audio_SetDeviceState(USB_AUDIO_STOPPING, 0U, 0U,
                                 USB_Device_LinkIsHighSpeed());
        USB_Device_Stop();
        USB_Audio_ReleaseHardware();
        USB_Audio_SetDeviceState(USB_AUDIO_OFF, 0U, 0U, 0U);
        active_mode = USB_DEVICE_MODE_OFF;
      }

      if ((active_mode == USB_DEVICE_MODE_OFF) &&
          (target_mode == USB_DEVICE_MODE_MSC))
      {
        if (SD_Card_IsPresent() == 0U)
        {
          msc_status = USB_MSC_NO_CARD;
        }
        else if ((AudioRecorder_IsBusy() != 0U) ||
                 (AudioPlayer_IsBusy() != 0U))
        {
          msc_status = USB_MSC_AUDIO_BUSY;
        }
        else
        {
          msc_status = USB_MSC_STARTING;
          USB_MSC_Publish(msc_status);
          SD_Storage_RequestUsbOwnership(1U);
          while ((usb_msc_requested != 0U) &&
                 (SD_Storage_IsUsbOwned() == 0U))
          {
            vTaskDelay(pdMS_TO_TICKS(10U));
          }
          if (usb_msc_requested == 0U)
          {
            SD_Storage_RequestUsbOwnership(0U);
            msc_status = USB_MSC_OFF;
          }
          else if (USB_Device_Start(USB_DEVICE_MODE_MSC,
                                    target_audio_host_mode) != 0U)
          {
            usb_msc_active = 1U;
            active_mode = USB_DEVICE_MODE_MSC;
            msc_status = USB_MSC_ACTIVE;
          }
          else
          {
            ++usb_msc_io_errors;
            SD_Storage_RequestUsbOwnership(0U);
            msc_status = USB_MSC_ERROR;
            usb_msc_requested = 0U;
          }
        }
      }
      else if ((active_mode == USB_DEVICE_MODE_OFF) &&
               (target_mode == USB_DEVICE_MODE_AUDIO))
      {
        if ((AudioRecorder_IsBusy() != 0U) ||
            (AudioPlayer_IsBusy() != 0U) ||
            (AudioSpectrum_IsBusy() != 0U))
        {
          USB_Audio_SetDeviceState(USB_AUDIO_BUSY, 0U, 0U, 0U);
        }
        else
        {
          USB_Audio_SetDeviceState(USB_AUDIO_STARTING, 0U, 0U, 0U);
          if ((USB_Audio_PrepareHardware() != 0U) &&
              (USB_Device_Start(USB_DEVICE_MODE_AUDIO,
                                target_audio_host_mode) != 0U))
          {
            active_mode = USB_DEVICE_MODE_AUDIO;
            active_audio_host_mode = target_audio_host_mode;
            USB_Audio_SetDeviceState(USB_AUDIO_ACTIVE, 1U, 0U, 0U);
          }
          else
          {
            USB_Audio_ReleaseHardware();
            USB_Audio_SetDeviceState(USB_AUDIO_ERROR, 0U, 0U, 0U);
            USB_Audio_RequestEnable(0U);
          }
        }
      }
    }

    if (active_mode == USB_DEVICE_MODE_MSC)
      msc_status = USB_MSC_ACTIVE;
    else if ((usb_msc_requested == 0U) &&
             (SD_Storage_IsUsbOwned() != 0U))
      SD_Storage_RequestUsbOwnership(0U);

    if (active_mode == USB_DEVICE_MODE_AUDIO)
    {
      USB_Audio_SetDeviceState(
          USB_AUDIO_ACTIVE, 1U,
          (hUsbDeviceHS.dev_state == USBD_STATE_CONFIGURED) ? 1U : 0U,
          USB_Device_LinkIsHighSpeed());
    }
    USB_MSC_Publish(msc_status);
    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100U));
  }
}
