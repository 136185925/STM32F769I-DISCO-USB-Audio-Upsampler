#ifndef USB_MSC_H
#define USB_MSC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum
{
  USB_MSC_OFF = 0,
  USB_MSC_STARTING,
  USB_MSC_ACTIVE,
  USB_MSC_STOPPING,
  USB_MSC_NO_CARD,
  USB_MSC_AUDIO_BUSY,
  USB_MSC_ERROR
} USB_MSC_Status;

typedef struct
{
  USB_MSC_Status status;
  uint32_t generation;
  uint32_t read_blocks;
  uint32_t written_blocks;
  uint32_t io_errors;
  uint8_t requested;
  uint8_t active;
  uint8_t configured;
  uint8_t card_present;
  uint8_t high_speed;
} USB_MSC_Snapshot;

void USB_MSC_CreateResources(void);
void USB_MSC_Task(void const *argument);
void USB_MSC_RequestEnable(uint8_t enable);
void USB_MSC_GetSnapshot(USB_MSC_Snapshot *snapshot);
uint8_t USB_MSC_IsActive(void);
void USB_MSC_WakeManager(void);

extern volatile uint32_t usb_msc_read_blocks;
extern volatile uint32_t usb_msc_written_blocks;
extern volatile uint32_t usb_msc_io_errors;

#ifdef __cplusplus
}
#endif

#endif /* USB_MSC_H */
