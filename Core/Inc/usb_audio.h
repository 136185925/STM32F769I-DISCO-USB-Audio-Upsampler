#ifndef USB_AUDIO_H
#define USB_AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define USB_AUDIO_INPUT_RING_FRAMES  32768U
#define USB_AUDIO_INPUT_RING_BYTES   (USB_AUDIO_INPUT_RING_FRAMES * 4U)
#define USB_AUDIO_START_FRAMES_MIN   6144U
#define USB_AUDIO_START_FRAMES_MAX   30720U
#define USB_AUDIO_START_FRAMES_STEP  1024U
#define USB_AUDIO_START_FRAMES_DEFAULT 8192U

typedef enum
{
  USB_AUDIO_OFF = 0,
  USB_AUDIO_STARTING,
  USB_AUDIO_ACTIVE,
  USB_AUDIO_STOPPING,
  USB_AUDIO_BUSY,
  USB_AUDIO_ERROR
} USB_AudioStatus;

typedef enum
{
  USB_AUDIO_OUTPUT_WM8994 = 0,
  USB_AUDIO_OUTPUT_SPDIF
} USB_AudioOutput;

typedef enum
{
  USB_AUDIO_HOST_LINUX = 0,   /* Audio Class 1, existing Linux/iPad path. */
  USB_AUDIO_HOST_WINDOWS      /* Audio Class 2, Windows usbaudio2.sys. */
} USB_AudioHostMode;

typedef enum
{
  USB_AUDIO_SPDIF_NATIVE = 0,
  USB_AUDIO_SPDIF_UPSAMPLE_4X = 1,
  USB_AUDIO_SPDIF_UPSAMPLE_4X_HEADROOM = 2,
  USB_AUDIO_SPDIF_REPEAT_4X = 3,
  USB_AUDIO_SPDIF_UPSAMPLE_4X_TPDF = 4,
  USB_AUDIO_SPDIF_UPSAMPLE_4X_IIR = 5,
  USB_AUDIO_SPDIF_UPSAMPLE_4X_HYBRID = 6
} USB_AudioSpdifMode;

typedef struct
{
  USB_AudioStatus status;
  uint32_t generation;
  uint32_t received_bytes;
  uint32_t received_packets;
  uint32_t underruns;
  uint32_t overruns;
  uint32_t sample_rate;
  uint32_t start_frames;
  uint8_t requested;
  uint8_t active;
  uint8_t configured;
  uint8_t streaming;
  uint8_t high_speed;
  uint8_t muted;
  uint8_t volume;
  USB_AudioOutput output;
  USB_AudioHostMode host_mode;
  USB_AudioSpdifMode spdif_mode;
} USB_AudioSnapshot;

void USB_Audio_CreateResources(void);
void USB_Audio_Task(void const *argument);
void USB_Audio_RequestEnable(uint8_t enable);
void USB_Audio_RequestVolume(uint8_t volume);
void USB_Audio_RequestOutput(USB_AudioOutput output);
USB_AudioOutput USB_Audio_GetOutput(void);
void USB_Audio_RequestHostMode(USB_AudioHostMode mode);
USB_AudioHostMode USB_Audio_GetHostMode(void);
void USB_Audio_RequestSpdifMode(USB_AudioSpdifMode mode);
USB_AudioSpdifMode USB_Audio_GetSpdifMode(void);
void USB_Audio_RequestStartFrames(uint32_t frames);
uint32_t USB_Audio_GetStartFrames(void);
void USB_Audio_GetSnapshot(USB_AudioSnapshot *snapshot);
uint8_t USB_Audio_IsActive(void);
uint8_t USB_Audio_ClaimsCodec(void);

/* USB device manager hooks. */
uint8_t USB_Audio_EnableRequested(void);
void USB_Audio_SetDeviceState(USB_AudioStatus status, uint8_t active,
                              uint8_t configured, uint8_t high_speed);
uint8_t USB_Audio_PrepareHardware(void);
void USB_Audio_ReleaseHardware(void);

/* SAI DMA callback hooks. */
uint8_t USB_Audio_DmaIsRunning(void);
void USB_Audio_DmaHalfCallback(void);
void USB_Audio_DmaFullCallback(void);
void USB_Audio_DmaErrorCallback(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_AUDIO_H */
