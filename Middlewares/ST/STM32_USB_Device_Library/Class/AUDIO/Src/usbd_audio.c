/**
  ******************************************************************************
  * @file    usbd_audio.c
  * @author  MCD Application Team
  * @brief   This file provides the Audio core functions.
  *
  *
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2015 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  * @verbatim
  *
  *          ===================================================================
  *                                AUDIO Class  Description
  *          ===================================================================
  *           This driver manages the Audio Class 1.0 following the "USB Device Class Definition for
  *           Audio Devices V1.0 Mar 18, 98".
  *           This driver implements the following aspects of the specification:
  *             - Device descriptor management
  *             - Configuration descriptor management
  *             - Standard AC Interface Descriptor management
  *             - 1 Audio Streaming Interface (with single channel, PCM, Stereo mode)
  *             - 1 Audio Streaming OUT Endpoint and 1 explicit Feedback IN Endpoint
  *             - 1 Audio Terminal Input (1 channel)
  *             - Audio Class-Specific AC Interfaces
  *             - Audio Class-Specific AS Interfaces
  *             - AudioControl Requests: only SET_CUR and GET_CUR requests are supported (for Mute)
  *             - Audio Feature Unit (limited to Mute control)
  *             - Audio Synchronization type: Asynchronous with explicit feedback
  *             - Two discrete audio sampling rates: 44.1 kHz and 48 kHz
  *          The current audio class version supports the following audio features:
  *             - Pulse Coded Modulation (PCM) format
  *             - sampling rate: 48KHz.
  *             - Bit resolution: 16
  *             - Number of channels: 2
  *             - No volume control
  *             - Mute/Unmute capability
  *             - Asynchronous Endpoints
  *
  * @note     In HS mode and when the DMA is used, all variables and data structures
  *           dealing with the DMA during the transaction process should be 32-bit aligned.
  *
  *
  *  @endverbatim
  ******************************************************************************
  */

/* BSPDependencies
- "stm32xxxxx_{eval}{discovery}.c"
- "stm32xxxxx_{eval}{discovery}_io.c"
- "stm32xxxxx_{eval}{discovery}_audio.c"
EndBSPDependencies */

/* Includes ------------------------------------------------------------------*/
#include "usbd_audio.h"
#include "usbd_ctlreq.h"


/** @addtogroup STM32_USB_DEVICE_LIBRARY
  * @{
  */


/** @defgroup USBD_AUDIO
  * @brief usbd core module
  * @{
  */

/** @defgroup USBD_AUDIO_Private_TypesDefinitions
  * @{
  */
/**
  * @}
  */


/** @defgroup USBD_AUDIO_Private_Defines
  * @{
  */
/**
  * @}
  */


/** @defgroup USBD_AUDIO_Private_Macros
  * @{
  */
#define AUDIO_SAMPLE_FREQ(frq) \
  (uint8_t)(frq), (uint8_t)((frq >> 8)), (uint8_t)((frq >> 16))

#define AUDIO_PACKET_SZE(frq) \
  (uint8_t)((((((frq) + 999U) / 1000U) + 1U) * 2U * 2U) & 0xFFU), \
  (uint8_t)(((((((frq) + 999U) / 1000U) + 1U) * 2U * 2U) >> 8U) & 0xFFU)

/* Audio Class 2.0 descriptor/control constants. The data stream deliberately
   stays 16-bit stereo so both protocols feed the same RTOS ring and SAI DMA. */
#define USB_AUDIO2_CONFIG_DESC_SIZ       0x98U
#define AUDIO2_PROTOCOL_02_00            0x20U
#define AUDIO2_CONTROL_CLOCK_SOURCE      0x0AU
#define AUDIO2_CLOCK_SOURCE_ID           0x10U
#define AUDIO2_CLOCK_VALID_CONTROL       0x02U
#define AUDIO2_CS_SAM_FREQ_CONTROL       0x01U
#define AUDIO2_CATEGORY_PRO_AUDIO        0x08U
#define AUDIO2_HS_BINTERVAL              0x04U
#define AUDIO2_FS_BINTERVAL              0x01U

#ifdef USE_USBD_COMPOSITE
#define AUDIO_PACKET_SZE_WORD(frq)     \
  (uint32_t)(((((frq) + 999U) / 1000U) + 1U) * 2U * 2U)
#endif /* USE_USBD_COMPOSITE  */
/**
  * @}
  */


/** @defgroup USBD_AUDIO_Private_FunctionPrototypes
  * @{
  */
static uint8_t USBD_AUDIO_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_AUDIO_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx);

static uint8_t USBD_AUDIO_Setup(USBD_HandleTypeDef *pdev,
                                USBD_SetupReqTypedef *req);
#ifndef USE_USBD_COMPOSITE
static uint8_t *USBD_AUDIO_GetCfgDesc(uint16_t *length);
static uint8_t *USBD_AUDIO_GetOtherSpeedCfgDesc(uint16_t *length);
static uint8_t *USBD_AUDIO_GetDeviceQualifierDesc(uint16_t *length);
static void USBD_AUDIO_PatchConfigSpeed(uint8_t *descriptor,
                                         uint16_t descriptor_size,
                                         USBD_SpeedTypeDef speed);
static void USBD_AUDIO2_PatchConfigSpeed(uint8_t *descriptor,
                                         uint16_t descriptor_size,
                                         USBD_SpeedTypeDef speed);
#endif /* USE_USBD_COMPOSITE  */
static uint8_t USBD_AUDIO_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_AUDIO_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_AUDIO_EP0_RxReady(USBD_HandleTypeDef *pdev);
static uint8_t USBD_AUDIO_EP0_TxReady(USBD_HandleTypeDef *pdev);
static uint8_t USBD_AUDIO_SOF(USBD_HandleTypeDef *pdev);

static uint8_t USBD_AUDIO_IsoINIncomplete(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_AUDIO_IsoOutIncomplete(USBD_HandleTypeDef *pdev, uint8_t epnum);
static void AUDIO_REQ_GetCurrent(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static void AUDIO_REQ_SetCurrent(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static void *USBD_AUDIO_GetAudioHeaderDesc(uint8_t *pConfDesc);
static uint8_t USBD_AUDIO_QueueFeedback(USBD_HandleTypeDef *pdev);
static void AUDIO2_REQ_GetCurrent(USBD_HandleTypeDef *pdev,
                                  USBD_SetupReqTypedef *req);
static void AUDIO2_REQ_GetRange(USBD_HandleTypeDef *pdev,
                                USBD_SetupReqTypedef *req);
static void AUDIO2_REQ_SetCurrent(USBD_HandleTypeDef *pdev,
                                  USBD_SetupReqTypedef *req);

#define AUDIO_FEEDBACK_FRAME_MASK 0x3FFFU

/**
  * @}
  */

/** @defgroup USBD_AUDIO_Private_Variables
  * @{
  */

USBD_ClassTypeDef USBD_AUDIO =
{
  USBD_AUDIO_Init,
  USBD_AUDIO_DeInit,
  USBD_AUDIO_Setup,
  USBD_AUDIO_EP0_TxReady,
  USBD_AUDIO_EP0_RxReady,
  USBD_AUDIO_DataIn,
  USBD_AUDIO_DataOut,
  USBD_AUDIO_SOF,
  USBD_AUDIO_IsoINIncomplete,
  USBD_AUDIO_IsoOutIncomplete,
#ifdef USE_USBD_COMPOSITE
  NULL,
  NULL,
  NULL,
  NULL,
#else
  USBD_AUDIO_GetCfgDesc,
  USBD_AUDIO_GetCfgDesc,
  USBD_AUDIO_GetOtherSpeedCfgDesc,
  USBD_AUDIO_GetDeviceQualifierDesc,
#endif /* USE_USBD_COMPOSITE  */
};

#ifndef USE_USBD_COMPOSITE
/* USB AUDIO device Configuration Descriptor */
__ALIGN_BEGIN static uint8_t USBD_AUDIO_CfgDesc[USB_AUDIO_CONFIG_DESC_SIZ] __ALIGN_END =
{
  /* Configuration 1 */
  0x09,                                 /* bLength */
  USB_DESC_TYPE_CONFIGURATION,          /* bDescriptorType */
  LOBYTE(USB_AUDIO_CONFIG_DESC_SIZ),    /* wTotalLength */
  HIBYTE(USB_AUDIO_CONFIG_DESC_SIZ),
  0x02,                                 /* bNumInterfaces */
  0x01,                                 /* bConfigurationValue */
  0x00,                                 /* iConfiguration */
#if (USBD_SELF_POWERED == 1U)
  0xC0,                                 /* bmAttributes: Bus Powered according to user configuration */
#else
  0x80,                                 /* bmAttributes: Bus Powered according to user configuration */
#endif /* USBD_SELF_POWERED */
  USBD_MAX_POWER,                       /* MaxPower (mA) */
  /* 09 byte*/

  /* USB Speaker Standard interface descriptor */
  AUDIO_INTERFACE_DESC_SIZE,            /* bLength */
  USB_DESC_TYPE_INTERFACE,              /* bDescriptorType */
  0x00,                                 /* bInterfaceNumber */
  0x00,                                 /* bAlternateSetting */
  0x00,                                 /* bNumEndpoints */
  USB_DEVICE_CLASS_AUDIO,               /* bInterfaceClass */
  AUDIO_SUBCLASS_AUDIOCONTROL,          /* bInterfaceSubClass */
  AUDIO_PROTOCOL_UNDEFINED,             /* bInterfaceProtocol */
  0x00,                                 /* iInterface */
  /* 09 byte*/

  /* USB Speaker Class-specific AC Interface Descriptor */
  AUDIO_INTERFACE_DESC_SIZE,            /* bLength */
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,      /* bDescriptorType */
  AUDIO_CONTROL_HEADER,                 /* bDescriptorSubtype */
  0x00,          /* 1.00 */             /* bcdADC */
  0x01,
  0x28,                                 /* wTotalLength */
  0x00,
  0x01,                                 /* bInCollection */
  0x01,                                 /* baInterfaceNr */
  /* 09 byte*/

  /* USB Speaker Input Terminal Descriptor */
  AUDIO_INPUT_TERMINAL_DESC_SIZE,       /* bLength */
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,      /* bDescriptorType */
  AUDIO_CONTROL_INPUT_TERMINAL,         /* bDescriptorSubtype */
  0x01,                                 /* bTerminalID */
  0x01,                                 /* wTerminalType AUDIO_TERMINAL_USB_STREAMING   0x0101 */
  0x01,
  0x00,                                 /* bAssocTerminal */
  0x02,                                 /* bNrChannels: stereo */
  0x03,                                 /* wChannelConfig: left + right */
  0x00,
  0x00,                                 /* iChannelNames */
  0x00,                                 /* iTerminal */
  /* 12 byte*/

  /* USB Speaker Audio Feature Unit Descriptor */
  0x0A,                                 /* bLength */
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,      /* bDescriptorType */
  AUDIO_CONTROL_FEATURE_UNIT,           /* bDescriptorSubtype */
  AUDIO_OUT_STREAMING_CTRL,             /* bUnitID */
  0x01,                                 /* bSourceID */
  0x01,                                 /* bControlSize */
  AUDIO_CONTROL_MUTE,                   /* bmaControls(0) */
  0,                                    /* bmaControls(left) */
  0,                                    /* bmaControls(right) */
  0x00,                                 /* iTerminal */
  /* 10 byte */

  /* USB Speaker Output Terminal Descriptor */
  0x09,      /* bLength */
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,      /* bDescriptorType */
  AUDIO_CONTROL_OUTPUT_TERMINAL,        /* bDescriptorSubtype */
  0x03,                                 /* bTerminalID */
  0x01,                                 /* wTerminalType  0x0301 */
  0x03,
  0x00,                                 /* bAssocTerminal */
  0x02,                                 /* bSourceID */
  0x00,                                 /* iTerminal */
  /* 09 byte */

  /* USB Speaker Standard AS Interface Descriptor - Audio Streaming Zero Bandwidth */
  /* Interface 1, Alternate Setting 0                                              */
  AUDIO_INTERFACE_DESC_SIZE,            /* bLength */
  USB_DESC_TYPE_INTERFACE,              /* bDescriptorType */
  0x01,                                 /* bInterfaceNumber */
  0x00,                                 /* bAlternateSetting */
  0x00,                                 /* bNumEndpoints */
  USB_DEVICE_CLASS_AUDIO,               /* bInterfaceClass */
  AUDIO_SUBCLASS_AUDIOSTREAMING,        /* bInterfaceSubClass */
  AUDIO_PROTOCOL_UNDEFINED,             /* bInterfaceProtocol */
  0x00,                                 /* iInterface */
  /* 09 byte*/

  /* USB Speaker Standard AS Interface Descriptor - Audio Streaming Operational */
  /* Interface 1, Alternate Setting 1                                           */
  AUDIO_INTERFACE_DESC_SIZE,            /* bLength */
  USB_DESC_TYPE_INTERFACE,              /* bDescriptorType */
  0x01,                                 /* bInterfaceNumber */
  0x01,                                 /* bAlternateSetting */
  0x02,                                 /* bNumEndpoints: audio OUT + feedback IN */
  USB_DEVICE_CLASS_AUDIO,               /* bInterfaceClass */
  AUDIO_SUBCLASS_AUDIOSTREAMING,        /* bInterfaceSubClass */
  AUDIO_PROTOCOL_UNDEFINED,             /* bInterfaceProtocol */
  0x00,                                 /* iInterface */
  /* 09 byte*/

  /* USB Speaker Audio Streaming Interface Descriptor */
  AUDIO_STREAMING_INTERFACE_DESC_SIZE,  /* bLength */
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,      /* bDescriptorType */
  AUDIO_STREAMING_GENERAL,              /* bDescriptorSubtype */
  0x01,                                 /* bTerminalLink */
  0x01,                                 /* bDelay */
  0x01,                                 /* wFormatTag AUDIO_FORMAT_PCM  0x0001 */
  0x00,
  /* 07 byte*/

  /* USB Speaker Audio Type III Format Interface Descriptor */
  0x0E,                                 /* bLength */
  AUDIO_INTERFACE_DESCRIPTOR_TYPE,      /* bDescriptorType */
  AUDIO_STREAMING_FORMAT_TYPE,          /* bDescriptorSubtype */
  AUDIO_FORMAT_TYPE_I,                  /* bFormatType */
  0x02,                                 /* bNrChannels */
  0x02,                                 /* bSubFrameSize :  2 Bytes per frame (16bits) */
  16,                                   /* bBitResolution (16-bits per sample) */
  0x02,                                 /* bSamFreqType: two discrete rates */
  AUDIO_SAMPLE_FREQ(44100U),            /* 44.1 kHz */
  AUDIO_SAMPLE_FREQ(48000U),            /* 48 kHz */
  /* 14 byte*/

  /* Endpoint 1 - Standard Descriptor */
  AUDIO_STANDARD_ENDPOINT_DESC_SIZE,    /* bLength */
  USB_DESC_TYPE_ENDPOINT,               /* bDescriptorType */
  AUDIO_OUT_EP,                         /* bEndpointAddress 1 out endpoint */
  0x05,                                 /* Isochronous asynchronous OUT */
  AUDIO_PACKET_SZE(USBD_AUDIO_FREQ),    /* wMaxPacketSize in Bytes (Freq(Samples)*2(Stereo)*2(HalfWord)) */
  AUDIO_FS_BINTERVAL,                   /* bInterval */
  0x00,                                 /* bRefresh */
  AUDIO_FEEDBACK_EP,                    /* bSynchAddress */
  /* 09 byte*/

  /* Endpoint - Audio Streaming Descriptor */
  AUDIO_STREAMING_ENDPOINT_DESC_SIZE,   /* bLength */
  AUDIO_ENDPOINT_DESCRIPTOR_TYPE,       /* bDescriptorType */
  AUDIO_ENDPOINT_GENERAL,               /* bDescriptor */
  0x01,                                 /* Sampling-frequency control supported */
  0x00,                                 /* bLockDelayUnits */
  0x00,                                 /* wLockDelay */
  0x00,
  /* 07 byte*/

  /* Explicit asynchronous feedback endpoint. 0x11 means isochronous,
     no synchronization type, feedback usage. The speed callback patches
     wMaxPacketSize and bInterval for HS (16.16) or FS (10.14). */
  AUDIO_STANDARD_ENDPOINT_DESC_SIZE,    /* bLength */
  USB_DESC_TYPE_ENDPOINT,               /* bDescriptorType */
  AUDIO_FEEDBACK_EP,                    /* bEndpointAddress */
  0x11,                                 /* Isochronous feedback IN */
  AUDIO_FEEDBACK_HS_PACKET_SIZE, 0x00,  /* wMaxPacketSize */
  AUDIO_HS_BINTERVAL,                   /* bInterval */
  AUDIO_FEEDBACK_FS_REFRESH,            /* bRefresh: patched for bus speed */
  0x00,                                 /* bSynchAddress */
  /* 09 byte*/
} ;

/* USB Audio Class 2.0, asynchronous stereo sink. This descriptor follows the
   Windows usbaudio2.sys topology requirements: one IAD, one programmable
   Clock Source, a valid clock path for both terminals, and an explicit
   feedback endpoint in the non-zero AudioStreaming alternate setting. */
__ALIGN_BEGIN static uint8_t
USBD_AUDIO2_CfgDesc[USB_AUDIO2_CONFIG_DESC_SIZ] __ALIGN_END =
{
  /* Configuration */
  0x09, USB_DESC_TYPE_CONFIGURATION,
  LOBYTE(USB_AUDIO2_CONFIG_DESC_SIZ), HIBYTE(USB_AUDIO2_CONFIG_DESC_SIZ),
  0x02, 0x01, 0x00,
#if (USBD_SELF_POWERED == 1U)
  0xC0,
#else
  0x80,
#endif
  USBD_MAX_POWER,

  /* Interface Association: AudioControl + AudioStreaming. */
  0x08, 0x0B, 0x00, 0x02, USB_DEVICE_CLASS_AUDIO,
  0x00, AUDIO2_PROTOCOL_02_00, 0x00,

  /* Standard AudioControl interface, alternate zero. */
  0x09, USB_DESC_TYPE_INTERFACE, 0x00, 0x00, 0x00,
  USB_DEVICE_CLASS_AUDIO, AUDIO_SUBCLASS_AUDIOCONTROL,
  AUDIO2_PROTOCOL_02_00, 0x00,

  /* Class-specific AC header; wTotalLength covers all five AC entities. */
  0x09, AUDIO_INTERFACE_DESCRIPTOR_TYPE, AUDIO_CONTROL_HEADER,
  0x00, 0x02, AUDIO2_CATEGORY_PRO_AUDIO, 0x40, 0x00, 0x00,

  /* Clock Source 0x10: internal programmable, frequency R/W, validity R/O. */
  0x08, AUDIO_INTERFACE_DESCRIPTOR_TYPE, AUDIO2_CONTROL_CLOCK_SOURCE,
  AUDIO2_CLOCK_SOURCE_ID, 0x03, 0x07, 0x00, 0x00,

  /* USB streaming Input Terminal 1, stereo L/R, clocked by source 0x10. */
  0x11, AUDIO_INTERFACE_DESCRIPTOR_TYPE, AUDIO_CONTROL_INPUT_TERMINAL,
  0x01, 0x01, 0x01, 0x00, AUDIO2_CLOCK_SOURCE_ID, 0x02,
  0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

  /* Feature Unit 2: master mute is readable and writable. */
  0x12, AUDIO_INTERFACE_DESCRIPTOR_TYPE, AUDIO_CONTROL_FEATURE_UNIT,
  AUDIO_OUT_STREAMING_CTRL, 0x01,
  0x03, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00,

  /* Speaker Output Terminal 3, sourced from Feature Unit 2. */
  0x0C, AUDIO_INTERFACE_DESCRIPTOR_TYPE, AUDIO_CONTROL_OUTPUT_TERMINAL,
  0x03, 0x01, 0x03, 0x00, AUDIO_OUT_STREAMING_CTRL,
  AUDIO2_CLOCK_SOURCE_ID, 0x00, 0x00, 0x00,

  /* AudioStreaming interface, zero bandwidth alternate setting. */
  0x09, USB_DESC_TYPE_INTERFACE, 0x01, 0x00, 0x00,
  USB_DEVICE_CLASS_AUDIO, AUDIO_SUBCLASS_AUDIOSTREAMING,
  AUDIO2_PROTOCOL_02_00, 0x00,

  /* AudioStreaming operational alternate setting. */
  0x09, USB_DESC_TYPE_INTERFACE, 0x01, 0x01, 0x02,
  USB_DEVICE_CLASS_AUDIO, AUDIO_SUBCLASS_AUDIOSTREAMING,
  AUDIO2_PROTOCOL_02_00, 0x00,

  /* UAC2 AS General: terminal 1, Type I PCM, stereo L/R. */
  0x10, AUDIO_INTERFACE_DESCRIPTOR_TYPE, AUDIO_STREAMING_GENERAL,
  0x01, 0x00, AUDIO_FORMAT_TYPE_I,
  0x01, 0x00, 0x00, 0x00,
  0x02, 0x03, 0x00, 0x00, 0x00, 0x00,

  /* Type I format: two-byte subslots, 16 valid bits. */
  0x06, AUDIO_INTERFACE_DESCRIPTOR_TYPE, AUDIO_STREAMING_FORMAT_TYPE,
  AUDIO_FORMAT_TYPE_I, 0x02, 0x10,

  /* Asynchronous Audio OUT endpoint; HS interval is one millisecond. */
  0x07, USB_DESC_TYPE_ENDPOINT, AUDIO_OUT_EP, 0x05,
  AUDIO_PACKET_SZE(USBD_AUDIO_FREQ), AUDIO2_HS_BINTERVAL,

  /* UAC2 class-specific AS isochronous data endpoint. */
  0x08, AUDIO_ENDPOINT_DESCRIPTOR_TYPE, AUDIO_ENDPOINT_GENERAL,
  0x00, 0x00, 0x00, 0x00, 0x00,

  /* Explicit asynchronous feedback endpoint, 16.16 at High Speed. */
  0x07, USB_DESC_TYPE_ENDPOINT, AUDIO_FEEDBACK_EP, 0x11,
  AUDIO_FEEDBACK_HS_PACKET_SIZE, 0x00, AUDIO2_HS_BINTERVAL
};

/* The USB core changes byte 1 of the Other-Speed descriptor to 0x07. Keep a
   separate copy so that request cannot corrupt the active configuration. */
__ALIGN_BEGIN static uint8_t
USBD_AUDIO_OtherSpeedCfgDesc[USB_AUDIO_CONFIG_DESC_SIZ] __ALIGN_END;
__ALIGN_BEGIN static uint8_t
USBD_AUDIO2_OtherSpeedCfgDesc[USB_AUDIO2_CONFIG_DESC_SIZ] __ALIGN_END;

/* USB Standard Device Descriptor */
__ALIGN_BEGIN static uint8_t USBD_AUDIO_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END =
{
  USB_LEN_DEV_QUALIFIER_DESC,
  USB_DESC_TYPE_DEVICE_QUALIFIER,
  0x00,
  0x02,
  0x00,
  0x00,
  0x00,
  0x40,
  0x01,
  0x00,
};

__ALIGN_BEGIN static uint8_t
USBD_AUDIO2_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END =
{
  USB_LEN_DEV_QUALIFIER_DESC, USB_DESC_TYPE_DEVICE_QUALIFIER,
  0x00, 0x02, 0xEF, 0x02, 0x01, 0x40, 0x01, 0x00
};
#endif /* USE_USBD_COMPOSITE  */

static uint8_t AUDIOOutEpAdd = AUDIO_OUT_EP;
static uint8_t AUDIOFeedbackEpAdd = AUDIO_FEEDBACK_EP;
static uint32_t USBD_AUDIO_CurrentFrequency = USBD_AUDIO_FREQ;
static USBD_AUDIO_ProtocolTypeDef USBD_AUDIO_Protocol =
    USBD_AUDIO_PROTOCOL_UAC1;

volatile uint32_t usbd_audio_feedback_tx_count = 0U;
volatile uint32_t usbd_audio_feedback_complete_count = 0U;
volatile uint32_t usbd_audio_feedback_error_count = 0U;
volatile uint32_t usbd_audio_feedback_last_q16 = 0U;

void USBD_AUDIO_SetProtocol(USBD_AUDIO_ProtocolTypeDef protocol)
{
  USBD_AUDIO_Protocol =
      (protocol == USBD_AUDIO_PROTOCOL_UAC2) ? USBD_AUDIO_PROTOCOL_UAC2 :
                                               USBD_AUDIO_PROTOCOL_UAC1;
}

USBD_AUDIO_ProtocolTypeDef USBD_AUDIO_GetProtocol(void)
{
  return USBD_AUDIO_Protocol;
}

void USBD_AUDIO_SetSpeed(USBD_SpeedTypeDef speed)
{
#ifndef USE_USBD_COMPOSITE
  if (USBD_AUDIO_Protocol == USBD_AUDIO_PROTOCOL_UAC2)
  {
    USBD_AUDIO2_PatchConfigSpeed(USBD_AUDIO2_CfgDesc,
                                 sizeof(USBD_AUDIO2_CfgDesc), speed);
    (void)USBD_memcpy(USBD_AUDIO2_OtherSpeedCfgDesc,
                      USBD_AUDIO2_CfgDesc, sizeof(USBD_AUDIO2_CfgDesc));
    USBD_AUDIO2_OtherSpeedCfgDesc[1U] = USB_DESC_TYPE_CONFIGURATION;
    USBD_AUDIO2_PatchConfigSpeed(
        USBD_AUDIO2_OtherSpeedCfgDesc,
        sizeof(USBD_AUDIO2_OtherSpeedCfgDesc),
        (speed == USBD_SPEED_HIGH) ? USBD_SPEED_FULL : USBD_SPEED_HIGH);
  }
  else
  {
    USBD_AUDIO_PatchConfigSpeed(USBD_AUDIO_CfgDesc,
                                sizeof(USBD_AUDIO_CfgDesc), speed);
    (void)USBD_memcpy(USBD_AUDIO_OtherSpeedCfgDesc,
                      USBD_AUDIO_CfgDesc, sizeof(USBD_AUDIO_CfgDesc));
    USBD_AUDIO_OtherSpeedCfgDesc[1U] = USB_DESC_TYPE_CONFIGURATION;
    USBD_AUDIO_PatchConfigSpeed(
        USBD_AUDIO_OtherSpeedCfgDesc,
        sizeof(USBD_AUDIO_OtherSpeedCfgDesc),
        (speed == USBD_SPEED_HIGH) ? USBD_SPEED_FULL : USBD_SPEED_HIGH);
  }
#else
  UNUSED(speed);
#endif
}

#ifndef USE_USBD_COMPOSITE
static void USBD_AUDIO_PatchConfigSpeed(uint8_t *descriptor,
                                        uint16_t descriptor_size,
                                        USBD_SpeedTypeDef speed)
{
  uint16_t offset = 0U;
  while ((offset + USB_LEN_EP_DESC) <= descriptor_size)
  {
    uint8_t length = descriptor[offset];
    if (length == 0U) break;
    if ((descriptor[offset + 1U] == USB_DESC_TYPE_ENDPOINT) &&
        (descriptor[offset + 2U] == AUDIO_OUT_EP))
    {
      descriptor[offset + 6U] =
          (speed == USBD_SPEED_HIGH) ? AUDIO_HS_BINTERVAL : AUDIO_FS_BINTERVAL;
    }
    else if ((descriptor[offset + 1U] == USB_DESC_TYPE_ENDPOINT) &&
             (descriptor[offset + 2U] == AUDIO_FEEDBACK_EP))
    {
      descriptor[offset + 4U] =
          (speed == USBD_SPEED_HIGH) ? AUDIO_FEEDBACK_HS_PACKET_SIZE :
                                      AUDIO_FEEDBACK_FS_PACKET_SIZE;
      descriptor[offset + 5U] = 0U;
      descriptor[offset + 6U] =
          (speed == USBD_SPEED_HIGH) ? AUDIO_HS_BINTERVAL : AUDIO_FS_BINTERVAL;
      descriptor[offset + 7U] =
          (speed == USBD_SPEED_HIGH) ? AUDIO_FEEDBACK_HS_REFRESH :
                                      AUDIO_FEEDBACK_FS_REFRESH;
    }
    offset += length;
  }
}

static void USBD_AUDIO2_PatchConfigSpeed(uint8_t *descriptor,
                                         uint16_t descriptor_size,
                                         USBD_SpeedTypeDef speed)
{
  uint16_t offset = 0U;
  while ((offset + USB_LEN_EP_DESC) <= descriptor_size)
  {
    const uint8_t length = descriptor[offset];
    if (length == 0U) break;
    if ((descriptor[offset + 1U] == USB_DESC_TYPE_ENDPOINT) &&
        (descriptor[offset + 2U] == AUDIO_OUT_EP))
    {
      descriptor[offset + 6U] =
          (speed == USBD_SPEED_HIGH) ? AUDIO2_HS_BINTERVAL :
                                      AUDIO2_FS_BINTERVAL;
    }
    else if ((descriptor[offset + 1U] == USB_DESC_TYPE_ENDPOINT) &&
             (descriptor[offset + 2U] == AUDIO_FEEDBACK_EP))
    {
      descriptor[offset + 4U] =
          (speed == USBD_SPEED_HIGH) ? AUDIO_FEEDBACK_HS_PACKET_SIZE :
                                      AUDIO_FEEDBACK_FS_PACKET_SIZE;
      descriptor[offset + 5U] = 0U;
      descriptor[offset + 6U] =
          (speed == USBD_SPEED_HIGH) ? AUDIO2_HS_BINTERVAL :
                                      AUDIO2_FS_BINTERVAL;
    }
    offset += length;
  }
}
#endif /* USE_USBD_COMPOSITE */
/**
  * @}
  */

/** @defgroup USBD_AUDIO_Private_Functions
  * @{
  */

/**
  * @brief  USBD_AUDIO_Init
  *         Initialize the AUDIO interface
  * @param  pdev: device instance
  * @param  cfgidx: Configuration index
  * @retval status
  */
static uint8_t USBD_AUDIO_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  UNUSED(cfgidx);
  USBD_AUDIO_HandleTypeDef *haudio;

  /* Allocate Audio structure */
  haudio = (USBD_AUDIO_HandleTypeDef *)USBD_malloc(sizeof(USBD_AUDIO_HandleTypeDef));

  if (haudio == NULL)
  {
    pdev->pClassDataCmsit[pdev->classId] = NULL;
    return (uint8_t)USBD_EMEM;
  }

  pdev->pClassDataCmsit[pdev->classId] = (void *)haudio;
  pdev->pClassData = pdev->pClassDataCmsit[pdev->classId];

#ifdef USE_USBD_COMPOSITE
  /* Get the Endpoints addresses allocated for this class instance */
  AUDIOOutEpAdd = USBD_CoreGetEPAdd(pdev, USBD_EP_OUT, USBD_EP_TYPE_ISOC, (uint8_t)pdev->classId);
#endif /* USE_USBD_COMPOSITE */

  AUDIOFeedbackEpAdd = AUDIO_FEEDBACK_EP;

  if ((USBD_AUDIO_Protocol == USBD_AUDIO_PROTOCOL_UAC2) &&
      (pdev->dev_speed == USBD_SPEED_HIGH))
  {
    pdev->ep_out[AUDIOOutEpAdd & 0xFU].bInterval = AUDIO2_HS_BINTERVAL;
  }
  else if (pdev->dev_speed == USBD_SPEED_HIGH)
  {
    pdev->ep_out[AUDIOOutEpAdd & 0xFU].bInterval = AUDIO_HS_BINTERVAL;
  }
  else   /* LOW and FULL-speed endpoints */
  {
    pdev->ep_out[AUDIOOutEpAdd & 0xFU].bInterval = AUDIO_FS_BINTERVAL;
  }

  /* Open EP OUT */
  (void)USBD_LL_OpenEP(pdev, AUDIOOutEpAdd, USBD_EP_TYPE_ISOC, AUDIO_OUT_PACKET);
  pdev->ep_out[AUDIOOutEpAdd & 0xFU].is_used = 1U;

  /* Feedback shares endpoint number 1 in the opposite direction. Audio and
     MSC are mutually exclusive, so both modes reuse the same EP1 IN FIFO. */
  (void)USBD_LL_OpenEP(pdev, AUDIOFeedbackEpAdd, USBD_EP_TYPE_ISOC,
                       (pdev->dev_speed == USBD_SPEED_HIGH) ?
                       AUDIO_FEEDBACK_HS_PACKET_SIZE : AUDIO_FEEDBACK_FS_PACKET_SIZE);
  pdev->ep_in[AUDIOFeedbackEpAdd & 0xFU].is_used = 1U;
  pdev->ep_in[AUDIOFeedbackEpAdd & 0xFU].bInterval =
      ((USBD_AUDIO_Protocol == USBD_AUDIO_PROTOCOL_UAC2) &&
       (pdev->dev_speed == USBD_SPEED_HIGH)) ? AUDIO2_HS_BINTERVAL :
      ((pdev->dev_speed == USBD_SPEED_HIGH) ? AUDIO_HS_BINTERVAL :
                                              AUDIO_FS_BINTERVAL);

  haudio->alt_setting = 0U;
  haudio->offset = AUDIO_OFFSET_UNKNOWN;
  haudio->wr_ptr = 0U;
  haudio->rd_ptr = 0U;
  haudio->rd_enable = 0U;
  haudio->feedback_sof_countdown = 0U;
  haudio->feedback_target_frame = 0U;
  haudio->feedback_target_valid = 0U;
  haudio->feedback_busy = 0U;
  haudio->mute = 0U;
  USBD_AUDIO_CurrentFrequency = USBD_AUDIO_FREQ;
  usbd_audio_feedback_tx_count = 0U;
  usbd_audio_feedback_complete_count = 0U;
  usbd_audio_feedback_error_count = 0U;
  usbd_audio_feedback_last_q16 = USBD_AUDIO_FREQ << 16U;

  /* Initialize the Audio output Hardware layer */
  if (((USBD_AUDIO_ItfTypeDef *)pdev->pUserData[pdev->classId])->Init(USBD_AUDIO_CurrentFrequency,
                                                                      AUDIO_DEFAULT_VOLUME,
                                                                      0U) != 0U)
  {
    return (uint8_t)USBD_FAIL;
  }

  /* Prepare Out endpoint to receive 1st packet */
  (void)USBD_LL_PrepareReceive(pdev, AUDIOOutEpAdd, haudio->buffer,
                               AUDIO_OUT_PACKET);

  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_AUDIO_Init
  *         DeInitialize the AUDIO layer
  * @param  pdev: device instance
  * @param  cfgidx: Configuration index
  * @retval status
  */
static uint8_t USBD_AUDIO_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  UNUSED(cfgidx);

#ifdef USE_USBD_COMPOSITE
  /* Get the Endpoints addresses allocated for this class instance */
  AUDIOOutEpAdd = USBD_CoreGetEPAdd(pdev, USBD_EP_OUT, USBD_EP_TYPE_ISOC, (uint8_t)pdev->classId);
#endif /* USE_USBD_COMPOSITE */

  /* Open EP OUT */
  (void)USBD_LL_CloseEP(pdev, AUDIOOutEpAdd);
  pdev->ep_out[AUDIOOutEpAdd & 0xFU].is_used = 0U;
  pdev->ep_out[AUDIOOutEpAdd & 0xFU].bInterval = 0U;

  (void)USBD_LL_CloseEP(pdev, AUDIOFeedbackEpAdd);
  pdev->ep_in[AUDIOFeedbackEpAdd & 0xFU].is_used = 0U;
  pdev->ep_in[AUDIOFeedbackEpAdd & 0xFU].bInterval = 0U;

  /* DeInit  physical Interface components */
  if (pdev->pClassDataCmsit[pdev->classId] != NULL)
  {
    ((USBD_AUDIO_ItfTypeDef *)pdev->pUserData[pdev->classId])->DeInit(0U);
    (void)USBD_free(pdev->pClassDataCmsit[pdev->classId]);
    pdev->pClassDataCmsit[pdev->classId] = NULL;
    pdev->pClassData = NULL;
  }

  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_AUDIO_Setup
  *         Handle the AUDIO specific requests
  * @param  pdev: instance
  * @param  req: usb requests
  * @retval status
  */
static uint8_t USBD_AUDIO_Setup(USBD_HandleTypeDef *pdev,
                                USBD_SetupReqTypedef *req)
{
  USBD_AUDIO_HandleTypeDef *haudio;
  uint16_t len;
  uint8_t *pbuf;
  uint16_t status_info = 0U;
  USBD_StatusTypeDef ret = USBD_OK;

  haudio = (USBD_AUDIO_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

  if (haudio == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  switch (req->bmRequest & USB_REQ_TYPE_MASK)
  {
    case USB_REQ_TYPE_CLASS:
      if (USBD_AUDIO_Protocol == USBD_AUDIO_PROTOCOL_UAC2)
      {
        /* UAC2 uses CUR=0x01 and RANGE=0x02 for both directions. Windows
           sends GET_CUR as A1/01 and GET_RANGE as A1/02, so routing these
           through the UAC1 0x81/0x82 request table stalls enumeration. */
        switch (req->bRequest)
        {
          case AUDIO2_REQ_CUR:
            if ((req->bmRequest & AUDIO2_REQ_DIRECTION_IN) != 0U)
              AUDIO2_REQ_GetCurrent(pdev, req);
            else
              AUDIO2_REQ_SetCurrent(pdev, req);
            break;

          case AUDIO2_REQ_RANGE:
            if ((req->bmRequest & AUDIO2_REQ_DIRECTION_IN) != 0U)
            {
              AUDIO2_REQ_GetRange(pdev, req);
            }
            else
            {
              USBD_CtlError(pdev, req);
              ret = USBD_FAIL;
            }
            break;

          default:
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
            break;
        }
      }
      else
      {
        switch (req->bRequest)
        {
          case AUDIO_REQ_GET_CUR:
            AUDIO_REQ_GetCurrent(pdev, req);
            break;

          case AUDIO_REQ_SET_CUR:
            AUDIO_REQ_SetCurrent(pdev, req);
            break;

          case AUDIO_REQ_GET_RANGE:
            /* RANGE is not part of the retained UAC1 control path. */
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
            break;

          default:
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
            break;
        }
      }
      break;

    case USB_REQ_TYPE_STANDARD:
      switch (req->bRequest)
      {
        case USB_REQ_GET_STATUS:
          if (pdev->dev_state == USBD_STATE_CONFIGURED)
          {
            (void)USBD_CtlSendData(pdev, (uint8_t *)&status_info, 2U);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_GET_DESCRIPTOR:
          if ((req->wValue >> 8) == AUDIO_DESCRIPTOR_TYPE)
          {
            pbuf = (uint8_t *)USBD_AUDIO_GetAudioHeaderDesc(pdev->pConfDesc);
            if (pbuf != NULL)
            {
              len = MIN(USB_AUDIO_DESC_SIZ, req->wLength);
              (void)USBD_CtlSendData(pdev, pbuf, len);
            }
            else
            {
              USBD_CtlError(pdev, req);
              ret = USBD_FAIL;
            }
          }
          break;

        case USB_REQ_GET_INTERFACE:
          if (pdev->dev_state == USBD_STATE_CONFIGURED)
          {
            (void)USBD_CtlSendData(pdev, (uint8_t *)&haudio->alt_setting, 1U);
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_SET_INTERFACE:
          if (pdev->dev_state == USBD_STATE_CONFIGURED)
          {
            if (((uint8_t)req->wIndex == 0x01U) &&
                ((uint8_t)(req->wValue) <= 1U))
            {
              haudio->alt_setting = (uint8_t)(req->wValue);
              (void)USBD_LL_FlushEP(pdev, AUDIOFeedbackEpAdd);
              haudio->feedback_busy = 0U;
              haudio->feedback_target_valid = 0U;
              if (haudio->alt_setting != 0U)
              {
                /* Arm from SOF so the first packet is synchronized to the
                   controller microframe clock. Retry pacing will acquire the
                   host's feedback polling phase if this is not the right one. */
                haudio->feedback_sof_countdown = 2U;
              }
              else
              {
                haudio->feedback_sof_countdown = 0U;
              }
            }
            else
            {
              /* Call the error management function (command will be NAKed */
              USBD_CtlError(pdev, req);
              ret = USBD_FAIL;
            }
          }
          else
          {
            USBD_CtlError(pdev, req);
            ret = USBD_FAIL;
          }
          break;

        case USB_REQ_CLEAR_FEATURE:
          break;

        default:
          USBD_CtlError(pdev, req);
          ret = USBD_FAIL;
          break;
      }
      break;
    default:
      USBD_CtlError(pdev, req);
      ret = USBD_FAIL;
      break;
  }

  return (uint8_t)ret;
}

#ifndef USE_USBD_COMPOSITE
/**
  * @brief  USBD_AUDIO_GetCfgDesc
  *         return configuration descriptor
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t *USBD_AUDIO_GetCfgDesc(uint16_t *length)
{
  if (USBD_AUDIO_Protocol == USBD_AUDIO_PROTOCOL_UAC2)
  {
    *length = (uint16_t)sizeof(USBD_AUDIO2_CfgDesc);
    return USBD_AUDIO2_CfgDesc;
  }
  *length = (uint16_t)sizeof(USBD_AUDIO_CfgDesc);

  return USBD_AUDIO_CfgDesc;
}

static uint8_t *USBD_AUDIO_GetOtherSpeedCfgDesc(uint16_t *length)
{
  if (USBD_AUDIO_Protocol == USBD_AUDIO_PROTOCOL_UAC2)
  {
    *length = (uint16_t)sizeof(USBD_AUDIO2_OtherSpeedCfgDesc);
    return USBD_AUDIO2_OtherSpeedCfgDesc;
  }
  *length = (uint16_t)sizeof(USBD_AUDIO_OtherSpeedCfgDesc);
  return USBD_AUDIO_OtherSpeedCfgDesc;
}
#endif /* USE_USBD_COMPOSITE  */
/**
  * @brief  USBD_AUDIO_DataIn
  *         handle data IN Stage
  * @param  pdev: device instance
  * @param  epnum: endpoint index
  * @retval status
  */
static uint8_t USBD_AUDIO_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  USBD_AUDIO_HandleTypeDef *haudio;
  haudio = (USBD_AUDIO_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

  if ((haudio != NULL) &&
      (epnum == (AUDIOFeedbackEpAdd & 0x7FU)) &&
      (haudio->alt_setting != 0U))
  {
    const uint16_t current_frame =
        (uint16_t)(USB_Device_GetFrameNumber() & AUDIO_FEEDBACK_FRAME_MASK);
    const uint16_t lead =
        ((USBD_AUDIO_Protocol == USBD_AUDIO_PROTOCOL_UAC2) &&
         (pdev->dev_speed == USBD_SPEED_HIGH)) ? 7U :
        ((pdev->dev_speed == USBD_SPEED_HIGH) ? 15U : 1U);

    haudio->feedback_busy = 0U;
    haudio->feedback_sof_countdown = 0U;
    haudio->feedback_target_frame =
        (uint16_t)((current_frame + lead) & AUDIO_FEEDBACK_FRAME_MASK);
    haudio->feedback_target_valid = 1U;
    ++usbd_audio_feedback_complete_count;

    /* Queue one microframe before the next host token. UAC2 uses an explicit
       one-millisecond bInterval (8 HS microframes); the retained UAC1 path
       uses its two-millisecond bRefresh cadence (16 HS microframes). */
  }
  return (uint8_t)USBD_OK;
}

static uint8_t USBD_AUDIO_QueueFeedback(USBD_HandleTypeDef *pdev)
{
  USBD_AUDIO_HandleTypeDef *haudio;
  USBD_AUDIO_ItfTypeDef *interface;
  uint32_t sample_rate_q16;
  uint32_t feedback;
  uint16_t packet_size;

  haudio = (USBD_AUDIO_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
  interface = (USBD_AUDIO_ItfTypeDef *)pdev->pUserData[pdev->classId];
  if ((haudio == NULL) || (interface == NULL) ||
      (haudio->alt_setting == 0U))
  {
    return (uint8_t)USBD_FAIL;
  }
  if (haudio->feedback_busy != 0U)
  {
    return (uint8_t)USBD_OK;
  }

  sample_rate_q16 = (interface->GetFeedback != NULL) ?
                    interface->GetFeedback() :
                    (USBD_AUDIO_CurrentFrequency << 16U);
  usbd_audio_feedback_last_q16 = sample_rate_q16;

  if (pdev->dev_speed == USBD_SPEED_HIGH)
  {
    /* USB 2.0 HS feedback is 16.16 samples per 125 us microframe. */
    feedback = sample_rate_q16 / 8000U;
    haudio->feedback[0] = (uint8_t)feedback;
    haudio->feedback[1] = (uint8_t)(feedback >> 8U);
    haudio->feedback[2] = (uint8_t)(feedback >> 16U);
    haudio->feedback[3] = (uint8_t)(feedback >> 24U);
    packet_size = AUDIO_FEEDBACK_HS_PACKET_SIZE;
  }
  else
  {
    /* USB full-speed feedback is 10.14 samples per 1 ms frame. */
    feedback = sample_rate_q16 / 4000U;
    haudio->feedback[0] = (uint8_t)feedback;
    haudio->feedback[1] = (uint8_t)(feedback >> 8U);
    haudio->feedback[2] = (uint8_t)(feedback >> 16U);
    packet_size = AUDIO_FEEDBACK_FS_PACKET_SIZE;
  }

  if (USBD_LL_Transmit(pdev, AUDIOFeedbackEpAdd,
                       haudio->feedback, packet_size) != USBD_OK)
  {
    haudio->feedback_busy = 0U;
    haudio->feedback_sof_countdown = 2U;
    haudio->feedback_target_valid = 0U;
    ++usbd_audio_feedback_error_count;
    return (uint8_t)USBD_FAIL;
  }
  haudio->feedback_busy = 1U;
  haudio->feedback_sof_countdown = 0U;
  ++usbd_audio_feedback_tx_count;
  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_AUDIO_EP0_RxReady
  *         handle EP0 Rx Ready event
  * @param  pdev: device instance
  * @retval status
  */
static uint8_t USBD_AUDIO_EP0_RxReady(USBD_HandleTypeDef *pdev)
{
  USBD_AUDIO_HandleTypeDef *haudio;
  haudio = (USBD_AUDIO_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

  if (haudio == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  if (haudio->control.cmd == AUDIO_REQ_SET_CUR)
  {
    if (USBD_AUDIO_Protocol == USBD_AUDIO_PROTOCOL_UAC2)
    {
      if ((haudio->control.recipient == USB_REQ_RECIPIENT_INTERFACE) &&
          (haudio->control.unit == AUDIO2_CLOCK_SOURCE_ID) &&
          (haudio->control.selector == AUDIO2_CS_SAM_FREQ_CONTROL) &&
          (haudio->control.len >= 4U))
      {
        const uint32_t frequency =
            (uint32_t)haudio->control.data[0] |
            ((uint32_t)haudio->control.data[1] << 8U) |
            ((uint32_t)haudio->control.data[2] << 16U) |
            ((uint32_t)haudio->control.data[3] << 24U);
        if (((frequency == 44100U) || (frequency == 48000U)) &&
            (((USBD_AUDIO_ItfTypeDef *)pdev->pUserData[pdev->classId])->
             FrequencyCtl(frequency) == 0))
        {
          USBD_AUDIO_CurrentFrequency = frequency;
        }
      }
      else if ((haudio->control.recipient == USB_REQ_RECIPIENT_INTERFACE) &&
               (haudio->control.unit == AUDIO_OUT_STREAMING_CTRL) &&
               (haudio->control.selector == AUDIO_CONTROL_MUTE) &&
               (haudio->control.len >= 1U))
      {
        haudio->mute = (haudio->control.data[0] != 0U) ? 1U : 0U;
        ((USBD_AUDIO_ItfTypeDef *)pdev->pUserData[pdev->classId])->
            MuteCtl(haudio->mute);
      }
      haudio->control.cmd = 0U;
      haudio->control.len = 0U;
    }
    else if ((haudio->control.recipient == USB_REQ_RECIPIENT_ENDPOINT) &&
             (haudio->control.selector == AUDIO_CONTROL_SAMPLING_FREQ) &&
             (haudio->control.len >= 3U))
    {
      uint32_t frequency = (uint32_t)haudio->control.data[0] |
                           ((uint32_t)haudio->control.data[1] << 8) |
                           ((uint32_t)haudio->control.data[2] << 16);
      if (((frequency == 44100U) || (frequency == 48000U)) &&
          (((USBD_AUDIO_ItfTypeDef *)pdev->pUserData[pdev->classId])->FrequencyCtl(frequency) == 0))
      {
        USBD_AUDIO_CurrentFrequency = frequency;
      }
      haudio->control.cmd = 0U;
      haudio->control.len = 0U;
    }
    else if (haudio->control.unit == AUDIO_OUT_STREAMING_CTRL)
    {
      haudio->mute = (haudio->control.data[0] != 0U) ? 1U : 0U;
      ((USBD_AUDIO_ItfTypeDef *)pdev->pUserData[pdev->classId])->MuteCtl(haudio->mute);
      haudio->control.cmd = 0U;
      haudio->control.len = 0U;
    }
  }

  return (uint8_t)USBD_OK;
}
/**
  * @brief  USBD_AUDIO_EP0_TxReady
  *         handle EP0 TRx Ready event
  * @param  pdev: device instance
  * @retval status
  */
static uint8_t USBD_AUDIO_EP0_TxReady(USBD_HandleTypeDef *pdev)
{
  UNUSED(pdev);

  /* Only OUT control data are processed */
  return (uint8_t)USBD_OK;
}
/**
  * @brief  USBD_AUDIO_SOF
  *         handle SOF event
  * @param  pdev: device instance
  * @retval status
  */
static uint8_t USBD_AUDIO_SOF(USBD_HandleTypeDef *pdev)
{
  USBD_AUDIO_HandleTypeDef *haudio;

  haudio = (USBD_AUDIO_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
  if ((haudio != NULL) && (haudio->alt_setting != 0U) &&
      (haudio->feedback_busy == 0U))
  {
    if (haudio->feedback_target_valid != 0U)
    {
      const uint16_t current_frame =
          (uint16_t)(USB_Device_GetFrameNumber() & AUDIO_FEEDBACK_FRAME_MASK);
      const uint16_t elapsed =
          (uint16_t)((current_frame - haudio->feedback_target_frame) &
                     AUDIO_FEEDBACK_FRAME_MASK);

      /* elapsed==0 is the intended slot. A small positive wrapped difference
         means a delayed SOF callback passed it; queue immediately so normal
         incomplete recovery can reacquire the host phase. */
      if ((elapsed == 0U) || (elapsed < 0x2000U))
      {
        haudio->feedback_target_valid = 0U;
        (void)USBD_AUDIO_QueueFeedback(pdev);
      }
    }
    else if (haudio->feedback_sof_countdown != 0U)
    {
      --haudio->feedback_sof_countdown;
      if (haudio->feedback_sof_countdown == 0U)
      {
        (void)USBD_AUDIO_QueueFeedback(pdev);
      }
    }
    else
    {
      (void)USBD_AUDIO_QueueFeedback(pdev);
    }
  }

  return (uint8_t)USBD_OK;
}

/**
  * @brief  USBD_AUDIO_SOF
  *         handle SOF event
  * @param  pdev: device instance
  * @param  offset: audio offset
  * @retval status
  */
void USBD_AUDIO_Sync(USBD_HandleTypeDef *pdev, AUDIO_OffsetTypeDef offset)
{
  USBD_AUDIO_HandleTypeDef *haudio;
  uint32_t BufferSize = AUDIO_TOTAL_BUF_SIZE / 2U;

  if (pdev->pClassDataCmsit[pdev->classId] == NULL)
  {
    return;
  }

  haudio = (USBD_AUDIO_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

  haudio->offset = offset;

  if (haudio->rd_enable == 1U)
  {
    haudio->rd_ptr += (uint16_t)BufferSize;

    if (haudio->rd_ptr == AUDIO_TOTAL_BUF_SIZE)
    {
      /* roll back */
      haudio->rd_ptr = 0U;
    }
  }

  if (haudio->rd_ptr > haudio->wr_ptr)
  {
    if ((haudio->rd_ptr - haudio->wr_ptr) < AUDIO_OUT_PACKET)
    {
      BufferSize += 4U;
    }
    else
    {
      if ((haudio->rd_ptr - haudio->wr_ptr) > (AUDIO_TOTAL_BUF_SIZE - AUDIO_OUT_PACKET))
      {
        BufferSize -= 4U;
      }
    }
  }
  else
  {
    if ((haudio->wr_ptr - haudio->rd_ptr) < AUDIO_OUT_PACKET)
    {
      BufferSize -= 4U;
    }
    else
    {
      if ((haudio->wr_ptr - haudio->rd_ptr) > (AUDIO_TOTAL_BUF_SIZE - AUDIO_OUT_PACKET))
      {
        BufferSize += 4U;
      }
    }
  }

  if (haudio->offset == AUDIO_OFFSET_FULL)
  {
    ((USBD_AUDIO_ItfTypeDef *)pdev->pUserData[pdev->classId])->AudioCmd(&haudio->buffer[0],
                                                                        BufferSize, AUDIO_CMD_PLAY);
    haudio->offset = AUDIO_OFFSET_NONE;
  }
}

/**
  * @brief  USBD_AUDIO_IsoINIncomplete
  *         handle data ISO IN Incomplete event
  * @param  pdev: device instance
  * @param  epnum: endpoint index
  * @retval status
  */
static uint8_t USBD_AUDIO_IsoINIncomplete(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  USBD_AUDIO_HandleTypeDef *haudio;
  haudio = (USBD_AUDIO_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
  if ((haudio != NULL) &&
      (epnum == (AUDIOFeedbackEpAdd & 0x7FU)) &&
      (haudio->alt_setting != 0U))
  {
    haudio->feedback_busy = 0U;
    haudio->feedback_target_valid = 0U;
    /* The HAL has already aborted the missed isochronous transfer. Retry on
       the next SOF only; immediate re-submission here caused one incomplete
       interrupt per HS microframe while the host polls every 1-2 ms. */
    haudio->feedback_sof_countdown = 2U;
    ++usbd_audio_feedback_error_count;
  }

  return (uint8_t)USBD_OK;
}
/**
  * @brief  USBD_AUDIO_IsoOutIncomplete
  *         handle data ISO OUT Incomplete event
  * @param  pdev: device instance
  * @param  epnum: endpoint index
  * @retval status
  */
static uint8_t USBD_AUDIO_IsoOutIncomplete(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  USBD_AUDIO_HandleTypeDef *haudio;

  if (pdev->pClassDataCmsit[pdev->classId] == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  haudio = (USBD_AUDIO_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

  /* Prepare Out endpoint to receive next audio packet */
  (void)USBD_LL_PrepareReceive(pdev, epnum,
                               &haudio->buffer[0],
                               AUDIO_OUT_PACKET);

  return (uint8_t)USBD_OK;
}
/**
  * @brief  USBD_AUDIO_DataOut
  *         handle data OUT Stage
  * @param  pdev: device instance
  * @param  epnum: endpoint index
  * @retval status
  */
static uint8_t USBD_AUDIO_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  uint16_t PacketSize;
  USBD_AUDIO_HandleTypeDef *haudio;

#ifdef USE_USBD_COMPOSITE
  /* Get the Endpoints addresses allocated for this class instance */
  AUDIOOutEpAdd = USBD_CoreGetEPAdd(pdev, USBD_EP_OUT, USBD_EP_TYPE_ISOC, (uint8_t)pdev->classId);
#endif /* USE_USBD_COMPOSITE */

  haudio = (USBD_AUDIO_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

  if (haudio == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  if (epnum == AUDIOOutEpAdd)
  {
    /* Get received data packet length */
    PacketSize = (uint16_t)USBD_LL_GetRxDataSize(pdev, epnum);

    /* Packet received Callback */
    ((USBD_AUDIO_ItfTypeDef *)pdev->pUserData[pdev->classId])->PeriodicTC(&haudio->buffer[0],
                                                                          PacketSize, AUDIO_OUT_TC);

    /* Prepare Out endpoint to receive next audio packet */
    (void)USBD_LL_PrepareReceive(pdev, AUDIOOutEpAdd,
                                 &haudio->buffer[0],
                                 AUDIO_OUT_PACKET);
  }

  return (uint8_t)USBD_OK;
}

static void AUDIO2_StoreU32(uint8_t *destination, uint32_t value)
{
  destination[0] = (uint8_t)value;
  destination[1] = (uint8_t)(value >> 8U);
  destination[2] = (uint8_t)(value >> 16U);
  destination[3] = (uint8_t)(value >> 24U);
}

/* UAC2 moves sample-rate control from the data endpoint to a Clock Source
   entity. Windows requires GET_CUR and GET_RANGE on that entity before it
   exposes an AudioStreaming format. */
static void AUDIO2_REQ_GetCurrent(USBD_HandleTypeDef *pdev,
                                  USBD_SetupReqTypedef *req)
{
  USBD_AUDIO_HandleTypeDef *haudio =
      (USBD_AUDIO_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
  const uint8_t entity = HIBYTE(req->wIndex);
  const uint8_t selector = HIBYTE(req->wValue);

  if ((haudio == NULL) ||
      ((req->bmRequest & USB_REQ_RECIPIENT_MASK) !=
       USB_REQ_RECIPIENT_INTERFACE))
  {
    USBD_CtlError(pdev, req);
    return;
  }

  (void)USBD_memset(haudio->control.data, 0, USB_MAX_EP0_SIZE);
  if ((entity == AUDIO2_CLOCK_SOURCE_ID) &&
      (selector == AUDIO2_CS_SAM_FREQ_CONTROL))
  {
    AUDIO2_StoreU32(haudio->control.data, USBD_AUDIO_CurrentFrequency);
    (void)USBD_CtlSendData(pdev, haudio->control.data,
                           MIN(req->wLength, 4U));
  }
  else if ((entity == AUDIO2_CLOCK_SOURCE_ID) &&
           (selector == AUDIO2_CLOCK_VALID_CONTROL))
  {
    haudio->control.data[0] = 1U;
    (void)USBD_CtlSendData(pdev, haudio->control.data,
                           MIN(req->wLength, 1U));
  }
  else if ((entity == AUDIO_OUT_STREAMING_CTRL) &&
           (selector == AUDIO_CONTROL_MUTE))
  {
    haudio->control.data[0] = haudio->mute;
    (void)USBD_CtlSendData(pdev, haudio->control.data,
                           MIN(req->wLength, 1U));
  }
  else
  {
    USBD_CtlError(pdev, req);
  }
}

static void AUDIO2_REQ_GetRange(USBD_HandleTypeDef *pdev,
                                USBD_SetupReqTypedef *req)
{
  USBD_AUDIO_HandleTypeDef *haudio =
      (USBD_AUDIO_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
  const uint8_t entity = HIBYTE(req->wIndex);
  const uint8_t selector = HIBYTE(req->wValue);

  if ((haudio == NULL) ||
      ((req->bmRequest & USB_REQ_RECIPIENT_MASK) !=
       USB_REQ_RECIPIENT_INTERFACE) ||
      (entity != AUDIO2_CLOCK_SOURCE_ID) ||
      (selector != AUDIO2_CS_SAM_FREQ_CONTROL))
  {
    USBD_CtlError(pdev, req);
    return;
  }

  /* Two non-overlapping discrete subranges: MIN==MAX and RES==0. */
  (void)USBD_memset(haudio->control.data, 0, USB_MAX_EP0_SIZE);
  haudio->control.data[0] = 2U;
  haudio->control.data[1] = 0U;
  AUDIO2_StoreU32(&haudio->control.data[2], 44100U);
  AUDIO2_StoreU32(&haudio->control.data[6], 44100U);
  AUDIO2_StoreU32(&haudio->control.data[10], 0U);
  AUDIO2_StoreU32(&haudio->control.data[14], 48000U);
  AUDIO2_StoreU32(&haudio->control.data[18], 48000U);
  AUDIO2_StoreU32(&haudio->control.data[22], 0U);
  (void)USBD_CtlSendData(pdev, haudio->control.data,
                         MIN(req->wLength, 26U));
}

static void AUDIO2_REQ_SetCurrent(USBD_HandleTypeDef *pdev,
                                  USBD_SetupReqTypedef *req)
{
  USBD_AUDIO_HandleTypeDef *haudio =
      (USBD_AUDIO_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
  const uint8_t entity = HIBYTE(req->wIndex);
  const uint8_t selector = HIBYTE(req->wValue);
  uint16_t expected_length = 0U;

  if ((haudio == NULL) ||
      ((req->bmRequest & USB_REQ_RECIPIENT_MASK) !=
       USB_REQ_RECIPIENT_INTERFACE))
  {
    USBD_CtlError(pdev, req);
    return;
  }
  if ((entity == AUDIO2_CLOCK_SOURCE_ID) &&
      (selector == AUDIO2_CS_SAM_FREQ_CONTROL))
  {
    expected_length = 4U;
  }
  else if ((entity == AUDIO_OUT_STREAMING_CTRL) &&
           (selector == AUDIO_CONTROL_MUTE))
  {
    expected_length = 1U;
  }
  if ((expected_length == 0U) || (req->wLength != expected_length))
  {
    USBD_CtlError(pdev, req);
    return;
  }

  haudio->control.cmd = AUDIO_REQ_SET_CUR;
  haudio->control.len = (uint8_t)expected_length;
  haudio->control.unit = entity;
  haudio->control.selector = selector;
  haudio->control.recipient = USB_REQ_RECIPIENT_INTERFACE;
  (void)USBD_CtlPrepareRx(pdev, haudio->control.data, expected_length);
}

/**
  * @brief  AUDIO_Req_GetCurrent
  *         Handles the GET_CUR Audio control request.
  * @param  pdev: device instance
  * @param  req: setup class request
  * @retval status
  */
static void AUDIO_REQ_GetCurrent(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  USBD_AUDIO_HandleTypeDef *haudio;
  haudio = (USBD_AUDIO_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

  if (haudio == NULL)
  {
    return;
  }

  (void)USBD_memset(haudio->control.data, 0, USB_MAX_EP0_SIZE);
  if (((req->bmRequest & USB_REQ_RECIPIENT_MASK) == USB_REQ_RECIPIENT_ENDPOINT) &&
      (HIBYTE(req->wValue) == AUDIO_CONTROL_SAMPLING_FREQ))
  {
    haudio->control.data[0] = (uint8_t)USBD_AUDIO_CurrentFrequency;
    haudio->control.data[1] = (uint8_t)(USBD_AUDIO_CurrentFrequency >> 8);
    haudio->control.data[2] = (uint8_t)(USBD_AUDIO_CurrentFrequency >> 16);
    (void)USBD_CtlSendData(pdev, haudio->control.data, MIN(req->wLength, 3U));
  }
  else
  {
    /* Send the current mute state. */
    haudio->control.data[0] = haudio->mute;
    (void)USBD_CtlSendData(pdev, haudio->control.data,
                           MIN(req->wLength, USB_MAX_EP0_SIZE));
  }
}

/**
  * @brief  AUDIO_Req_SetCurrent
  *         Handles the SET_CUR Audio control request.
  * @param  pdev: device instance
  * @param  req: setup class request
  * @retval status
  */
static void AUDIO_REQ_SetCurrent(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  USBD_AUDIO_HandleTypeDef *haudio;
  haudio = (USBD_AUDIO_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];

  if (haudio == NULL)
  {
    return;
  }

  if (req->wLength != 0U)
  {
    haudio->control.cmd = AUDIO_REQ_SET_CUR;     /* Set the request value */
    haudio->control.len = (uint8_t)MIN(req->wLength, USB_MAX_EP0_SIZE);  /* Set the request data length */
    haudio->control.unit = HIBYTE(req->wIndex);  /* Set the request target unit */
    haudio->control.selector = HIBYTE(req->wValue);
    haudio->control.recipient = req->bmRequest & USB_REQ_RECIPIENT_MASK;

    /* Prepare the reception of the buffer over EP0 */
    (void)USBD_CtlPrepareRx(pdev, haudio->control.data, haudio->control.len);
  }
}

#ifndef USE_USBD_COMPOSITE
/**
  * @brief  DeviceQualifierDescriptor
  *         return Device Qualifier descriptor
  * @param  length : pointer data length
  * @retval pointer to descriptor buffer
  */
static uint8_t *USBD_AUDIO_GetDeviceQualifierDesc(uint16_t *length)
{
  if (USBD_AUDIO_Protocol == USBD_AUDIO_PROTOCOL_UAC2)
  {
    *length = (uint16_t)sizeof(USBD_AUDIO2_DeviceQualifierDesc);
    return USBD_AUDIO2_DeviceQualifierDesc;
  }
  *length = (uint16_t)sizeof(USBD_AUDIO_DeviceQualifierDesc);

  return USBD_AUDIO_DeviceQualifierDesc;
}
#endif /* USE_USBD_COMPOSITE  */
/**
  * @brief  USBD_AUDIO_RegisterInterface
  * @param  pdev: device instance
  * @param  fops: Audio interface callback
  * @retval status
  */
uint8_t USBD_AUDIO_RegisterInterface(USBD_HandleTypeDef *pdev,
                                     USBD_AUDIO_ItfTypeDef *fops)
{
  if (fops == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  pdev->pUserData[pdev->classId] = fops;

  return (uint8_t)USBD_OK;
}

#ifdef USE_USBD_COMPOSITE
/**
  * @brief  USBD_AUDIO_GetEpPcktSze
  * @param  pdev: device instance (reserved for future use)
  * @param  If: Interface number (reserved for future use)
  * @param  Ep: Endpoint number (reserved for future use)
  * @retval status
  */
uint32_t USBD_AUDIO_GetEpPcktSze(USBD_HandleTypeDef *pdev, uint8_t If, uint8_t Ep)
{
  uint32_t mps;

  UNUSED(pdev);
  UNUSED(If);
  UNUSED(Ep);

  mps = AUDIO_PACKET_SZE_WORD(USBD_AUDIO_FREQ);

  /* Return the wMaxPacketSize value in Bytes (Freq(Samples)*2(Stereo)*2(HalfWord)) */
  return mps;
}
#endif /* USE_USBD_COMPOSITE */

/**
  * @brief  USBD_AUDIO_GetAudioHeaderDesc
  *         This function return the Audio descriptor
  * @param  pdev: device instance
  * @param  pConfDesc:  pointer to Bos descriptor
  * @retval pointer to the Audio AC Header descriptor
  */
static void *USBD_AUDIO_GetAudioHeaderDesc(uint8_t *pConfDesc)
{
  USBD_ConfigDescTypeDef *desc = (USBD_ConfigDescTypeDef *)(void *)pConfDesc;
  USBD_DescHeaderTypeDef *pdesc = (USBD_DescHeaderTypeDef *)(void *)pConfDesc;
  uint8_t *pAudioDesc =  NULL;
  uint16_t ptr;

  if (desc->wTotalLength > desc->bLength)
  {
    ptr = desc->bLength;

    while (ptr < desc->wTotalLength)
    {
      pdesc = USBD_GetNextDesc((uint8_t *)pdesc, &ptr);
      if ((pdesc->bDescriptorType == AUDIO_INTERFACE_DESCRIPTOR_TYPE) &&
          (pdesc->bDescriptorSubType == AUDIO_CONTROL_HEADER))
      {
        pAudioDesc = (uint8_t *)pdesc;
        break;
      }
    }
  }
  return pAudioDesc;
}

/**
  * @}
  */


/**
  * @}
  */


/**
  * @}
  */
