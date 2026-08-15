#include "usbd_core.h"
#include "usbd_audio.h"
#include "usbd_msc.h"
#include "usb_otg.h"

/* One MSC instance is used. Keep its large transfer buffer statically
 * allocated so USB throughput does not depend on the small C heap. */
static union
{
  uint32_t align;
  USBD_MSC_BOT_HandleTypeDef msc;
  USBD_AUDIO_HandleTypeDef audio;
} usb_class_memory;

void *USB_Device_StaticMalloc(uint32_t size)
{
  if (size > sizeof(usb_class_memory)) return NULL;
  memset(&usb_class_memory, 0, sizeof(usb_class_memory));
  return &usb_class_memory;
}

void USB_Device_StaticFree(void *memory)
{
  (void)memory;
}

uint32_t USB_Device_GetFrameNumber(void)
{
  return hpcd_USB_OTG_HS.FrameNumber & 0x3FFFU;
}

void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *hpcd)
{
  USBD_LL_SetupStage(hpcd->pData, (uint8_t *)hpcd->Setup);
}

void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  USBD_LL_DataOutStage(hpcd->pData, epnum, hpcd->OUT_ep[epnum].xfer_buff);
}

void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  USBD_LL_DataInStage(hpcd->pData, epnum, hpcd->IN_ep[epnum].xfer_buff);
}

void HAL_PCD_SOFCallback(PCD_HandleTypeDef *hpcd) { USBD_LL_SOF(hpcd->pData); }

void HAL_PCD_ResetCallback(PCD_HandleTypeDef *hpcd)
{
  USBD_SpeedTypeDef speed = (hpcd->Init.speed == PCD_SPEED_HIGH) ?
                            USBD_SPEED_HIGH : USBD_SPEED_FULL;
  USBD_LL_Reset(hpcd->pData);
  USBD_LL_SetSpeed(hpcd->pData, speed);
  USBD_AUDIO_SetSpeed(speed);
}

void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *hpcd) { USBD_LL_Suspend(hpcd->pData); }
void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *hpcd) { USBD_LL_Resume(hpcd->pData); }
void HAL_PCD_ISOOUTIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  USBD_LL_IsoOUTIncomplete(hpcd->pData, epnum);
}
void HAL_PCD_ISOINIncompleteCallback(PCD_HandleTypeDef *hpcd, uint8_t epnum)
{
  USBD_LL_IsoINIncomplete(hpcd->pData, epnum);
}
void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *hpcd) { USBD_LL_DevConnected(hpcd->pData); }
void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *hpcd) { USBD_LL_DevDisconnected(hpcd->pData); }

USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *pdev)
{
  /* Link both layers before HAL_PCD_Init enables the OTG interrupt. */
  hpcd_USB_OTG_HS.pData = pdev;
  pdev->pData = &hpcd_USB_OTG_HS;
  MX_USB_OTG_HS_PCD_Init();
  HAL_PCDEx_SetRxFiFo(&hpcd_USB_OTG_HS, 0x200U);
  HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_HS, 0U, 0x40U);
  /* Use all 1024 FIFO words: RX 512 + EP0 TX 64 + EP1 IN TX 448.
     EP1 carries MSC data or the four-byte Audio feedback packet; the two
     device modes are mutually exclusive. */
  HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_HS, 1U, 0x1C0U);
  return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *pdev)
{
  return (HAL_PCD_DeInit((PCD_HandleTypeDef *)pdev->pData) == HAL_OK) ?
         USBD_OK : USBD_FAIL;
}
USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *pdev)
{
  return (HAL_PCD_Start((PCD_HandleTypeDef *)pdev->pData) == HAL_OK) ?
         USBD_OK : USBD_FAIL;
}
USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *pdev)
{
  return (HAL_PCD_Stop((PCD_HandleTypeDef *)pdev->pData) == HAL_OK) ?
         USBD_OK : USBD_FAIL;
}
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr,
                                  uint8_t ep_type, uint16_t ep_mps)
{
  return (HAL_PCD_EP_Open((PCD_HandleTypeDef *)pdev->pData, ep_addr, ep_mps,
                          ep_type) == HAL_OK) ? USBD_OK : USBD_FAIL;
}
USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  return (HAL_PCD_EP_Close((PCD_HandleTypeDef *)pdev->pData, ep_addr) == HAL_OK) ?
         USBD_OK : USBD_FAIL;
}
USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  return (HAL_PCD_EP_Flush((PCD_HandleTypeDef *)pdev->pData, ep_addr) == HAL_OK) ?
         USBD_OK : USBD_FAIL;
}
USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  return (HAL_PCD_EP_SetStall((PCD_HandleTypeDef *)pdev->pData, ep_addr) == HAL_OK) ?
         USBD_OK : USBD_FAIL;
}
USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  return (HAL_PCD_EP_ClrStall((PCD_HandleTypeDef *)pdev->pData, ep_addr) == HAL_OK) ?
         USBD_OK : USBD_FAIL;
}
uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  PCD_HandleTypeDef *hpcd = (PCD_HandleTypeDef *)pdev->pData;
  return ((ep_addr & 0x80U) != 0U) ? hpcd->IN_ep[ep_addr & 0x7FU].is_stall :
                                    hpcd->OUT_ep[ep_addr & 0x7FU].is_stall;
}
USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *pdev, uint8_t address)
{
  return (HAL_PCD_SetAddress((PCD_HandleTypeDef *)pdev->pData, address) == HAL_OK) ?
         USBD_OK : USBD_FAIL;
}
USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *pdev, uint8_t ep_addr,
                                    uint8_t *buffer, uint32_t size)
{
  return (HAL_PCD_EP_Transmit((PCD_HandleTypeDef *)pdev->pData, ep_addr, buffer,
                              size) == HAL_OK) ? USBD_OK : USBD_FAIL;
}
USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *pdev,
                                          uint8_t ep_addr, uint8_t *buffer,
                                          uint32_t size)
{
  return (HAL_PCD_EP_Receive((PCD_HandleTypeDef *)pdev->pData, ep_addr, buffer,
                             size) == HAL_OK) ? USBD_OK : USBD_FAIL;
}
uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
  return HAL_PCD_EP_GetRxCount((PCD_HandleTypeDef *)pdev->pData, ep_addr);
}
void USBD_LL_Delay(uint32_t delay) { HAL_Delay(delay); }
