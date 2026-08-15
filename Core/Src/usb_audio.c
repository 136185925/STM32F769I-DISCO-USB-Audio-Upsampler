#include "usb_audio.h"

#include "audio_player.h"
#include "stm32f769i_discovery_audio.h"
#include "spdif_iir_upsampler.h"
#include "spdif_tx.h"
#include "spdif_upsampler.h"
#include "touch.h"
#include "usb_msc.h"
#include "rtc.h"
#include "usbd_audio.h"
#include "usbd_audio_if.h"
#include "FreeRTOS.h"
#include "task.h"

#include <string.h>

#define USB_AUDIO_DEFAULT_SAMPLE_RATE  48000U
#define USB_AUDIO_RATE_44K              44100U
#define USB_AUDIO_RATE_48K              48000U
#define USB_AUDIO_DEFAULT_VOLUME       20U
#define USB_AUDIO_INPUT_FRAME_BYTES    4U
#define USB_AUDIO_INPUT_RING_MASK      (USB_AUDIO_INPUT_RING_FRAMES - 1U)
#define USB_AUDIO_DMA_FRAMES           4096U
#define USB_AUDIO_DMA_HALF_FRAMES      (USB_AUDIO_DMA_FRAMES / 2U)
#define USB_AUDIO_START_FRAMES         8192U
/* Keep a complete start-watermark in the elastic ring during steady state.
 * After the initial DMA fill the queue begins near 4096 frames, then explicit
 * feedback raises it to this target. The extra guard absorbs the 60-70 ms
 * isochronous OUT gaps observed when both Windows and iPad repeat SET_CUR for
 * the already active sample rate, without increasing initial playback delay. */
#define USB_AUDIO_FEEDBACK_TARGET_FRAMES USB_AUDIO_START_FRAMES
#define USB_AUDIO_FEEDBACK_FILTER_SHIFT 6U
#define USB_AUDIO_FEEDBACK_P_Q16        1024LL
#define USB_AUDIO_FEEDBACK_MAX_PPM      5000LL
#define USB_AUDIO_FEEDBACK_I_MAX_PPM    2500LL
#define USB_AUDIO_NOTIFY_DATA          (1UL << 0)
#define USB_AUDIO_NOTIFY_DMA_HALF      (1UL << 1)
#define USB_AUDIO_NOTIFY_DMA_FULL      (1UL << 2)
#define USB_AUDIO_NOTIFY_ERROR         (1UL << 3)
#define USB_AUDIO_NOTIFY_RATE          (1UL << 4)
#define USB_AUDIO_NOTIFY_VOLUME        (1UL << 5)
#define USB_AUDIO_NOTIFY_OUTPUT        (1UL << 6)
#define USB_AUDIO_NOTIFY_SPDIF_MODE    (1UL << 7)
#define USB_AUDIO_VOLUME_APPLY_MS      25U
#define USB_AUDIO_STATS_PUBLISH_MS     250U
#define USB_AUDIO_HOST_BKP_MAGIC       0x55414300UL
#define USB_AUDIO_HOST_BKP_MASK        0xFFFFFF00UL
#define USB_AUDIO_SPDIF_BKP_MAGIC      0x53504400UL
#define USB_AUDIO_SPDIF_BKP_MASK       0xFFFFFF00UL
#define USB_AUDIO_SPDIF_FACTOR         SPDIF_UPSAMPLER_FACTOR

#if USB_AUDIO_START_FRAMES >= USB_AUDIO_INPUT_RING_FRAMES
#error "USB audio start threshold must be smaller than the input ring"
#endif
#if (USB_AUDIO_INPUT_RING_FRAMES & USB_AUDIO_INPUT_RING_MASK) != 0U
#error "USB audio input ring capacity must be a power of two"
#endif
#if USB_AUDIO_START_FRAMES < USB_AUDIO_DMA_FRAMES
#error "USB audio start threshold must cover the complete DMA buffer"
#endif
#if (USB_AUDIO_DMA_HALF_FRAMES % USB_AUDIO_SPDIF_FACTOR) != 0U
#error "S/PDIF DMA half-buffer must contain complete 4x interpolation groups"
#endif

typedef struct
{
  int16_t left;
  int16_t right;
} USB_AudioFrame;

typedef union
{
  USB_AudioFrame pcm16[USB_AUDIO_DMA_FRAMES];
  uint32_t spdif[USB_AUDIO_DMA_FRAMES * 2U];
} USB_AudioDmaBuffer;

/* Only the SAI DMA buffer needs non-cacheable SRAM. USB ingress and the
 * elastic input ring are CPU-owned and remain in normal cached SRAM. */
__attribute__((section(".dma_buffer"), aligned(32)))
static USB_AudioDmaBuffer usb_audio_dma;

/* CPU-only elastic storage. Keep it in cached internal SRAM: neither USB nor
 * SAI DMA accesses this ring directly, so no cache maintenance is required. */
__attribute__((section(".internal_usb_audio"), aligned(32)))
static USB_AudioFrame usb_audio_input[USB_AUDIO_INPUT_RING_FRAMES];

static TaskHandle_t usb_audio_task_handle;
static USB_AudioSnapshot usb_audio_snapshot;
static volatile uint32_t usb_audio_write_frame;
static volatile uint32_t usb_audio_read_frame;
static volatile uint32_t usb_audio_received_bytes;
static volatile uint32_t usb_audio_received_packets;
static volatile uint32_t usb_audio_underruns;
static volatile uint32_t usb_audio_overruns;
static volatile uint8_t usb_audio_requested;
static volatile uint8_t usb_audio_active;
static volatile uint8_t usb_audio_configured;
static volatile uint8_t usb_audio_high_speed;
static volatile uint8_t usb_audio_prepared;
static volatile uint8_t usb_audio_dma_running;
static volatile uint8_t usb_audio_rebuffer_requested;
static volatile uint8_t usb_audio_muted;
static volatile uint8_t usb_audio_requested_volume = USB_AUDIO_DEFAULT_VOLUME;
static uint8_t usb_audio_applied_volume = USB_AUDIO_DEFAULT_VOLUME;
static uint8_t usb_audio_applied_mute;
static TickType_t usb_audio_last_volume_apply_tick;
static volatile USB_AudioStatus usb_audio_status;
static volatile uint32_t usb_audio_requested_rate = USB_AUDIO_DEFAULT_SAMPLE_RATE;
static volatile uint32_t usb_audio_current_rate = USB_AUDIO_DEFAULT_SAMPLE_RATE;
static volatile USB_AudioOutput usb_audio_output = USB_AUDIO_OUTPUT_WM8994;
static volatile USB_AudioHostMode usb_audio_host_mode = USB_AUDIO_HOST_LINUX;
static volatile USB_AudioSpdifMode usb_audio_spdif_mode =
    USB_AUDIO_SPDIF_NATIVE;
static USB_AudioOutput usb_audio_prepared_output = USB_AUDIO_OUTPUT_WM8994;
static USB_AudioSpdifMode usb_audio_prepared_spdif_mode =
    USB_AUDIO_SPDIF_NATIVE;
static TickType_t usb_audio_last_stats_publish_tick;
static volatile int32_t usb_audio_feedback_integral_q16;
static volatile int32_t usb_audio_feedback_filtered_queue_q8 =
    (int32_t)(USB_AUDIO_FEEDBACK_TARGET_FRAMES << 8U);
static uint32_t usb_audio_spdif_status_frame;
static SPDIF_Upsampler4x usb_audio_spdif_upsampler;
static SPDIF_IirUpsampler4x usb_audio_spdif_iir_upsampler;
static SPDIF_HybridUpsampler4x usb_audio_spdif_hybrid_upsampler;

static void USB_Audio_Publish(void)
{
  const TickType_t now = xTaskGetTickCount();

  taskENTER_CRITICAL();
  const uint8_t state_changed =
      ((usb_audio_snapshot.status != usb_audio_status) ||
       (usb_audio_snapshot.requested != usb_audio_requested) ||
       (usb_audio_snapshot.active != usb_audio_active) ||
       (usb_audio_snapshot.configured != usb_audio_configured) ||
       (usb_audio_snapshot.streaming != usb_audio_dma_running) ||
       (usb_audio_snapshot.high_speed != usb_audio_high_speed) ||
       (usb_audio_snapshot.muted != usb_audio_muted) ||
       (usb_audio_snapshot.volume != usb_audio_requested_volume) ||
       (usb_audio_snapshot.output != usb_audio_output) ||
       (usb_audio_snapshot.host_mode != usb_audio_host_mode) ||
       (usb_audio_snapshot.spdif_mode != usb_audio_spdif_mode) ||
       (usb_audio_snapshot.sample_rate != usb_audio_current_rate)) ? 1U : 0U;
  const uint8_t xrun_changed =
      ((usb_audio_snapshot.underruns != usb_audio_underruns) ||
       (usb_audio_snapshot.overruns != usb_audio_overruns)) ? 1U : 0U;
  const uint8_t stats_changed =
      ((usb_audio_snapshot.received_bytes != usb_audio_received_bytes) ||
       (usb_audio_snapshot.received_packets != usb_audio_received_packets)) ?
      1U : 0U;
  const uint8_t stats_due =
      ((stats_changed != 0U) &&
       ((now - usb_audio_last_stats_publish_tick) >=
        pdMS_TO_TICKS(USB_AUDIO_STATS_PUBLISH_MS))) ? 1U : 0U;

  usb_audio_snapshot.status = usb_audio_status;
  usb_audio_snapshot.requested = usb_audio_requested;
  usb_audio_snapshot.active = usb_audio_active;
  usb_audio_snapshot.configured = usb_audio_configured;
  usb_audio_snapshot.streaming = usb_audio_dma_running;
  usb_audio_snapshot.high_speed = usb_audio_high_speed;
  usb_audio_snapshot.muted = usb_audio_muted;
  usb_audio_snapshot.volume = usb_audio_requested_volume;
  usb_audio_snapshot.output = usb_audio_output;
  usb_audio_snapshot.host_mode = usb_audio_host_mode;
  usb_audio_snapshot.spdif_mode = usb_audio_spdif_mode;
  usb_audio_snapshot.sample_rate = usb_audio_current_rate;
  usb_audio_snapshot.received_bytes = usb_audio_received_bytes;
  usb_audio_snapshot.received_packets = usb_audio_received_packets;
  usb_audio_snapshot.underruns = usb_audio_underruns;
  usb_audio_snapshot.overruns = usb_audio_overruns;
  if ((state_changed != 0U) || (xrun_changed != 0U) || (stats_due != 0U))
  {
    ++usb_audio_snapshot.generation;
    usb_audio_last_stats_publish_tick = now;
  }
  taskEXIT_CRITICAL();
}

void USB_Audio_CreateResources(void)
{
  const uint32_t saved_host = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR1);
  const uint32_t saved_spdif = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR2);

  memset(&usb_audio_snapshot, 0, sizeof(usb_audio_snapshot));
  usb_audio_snapshot.status = USB_AUDIO_OFF;
  usb_audio_requested_rate = USB_AUDIO_DEFAULT_SAMPLE_RATE;
  usb_audio_current_rate = USB_AUDIO_DEFAULT_SAMPLE_RATE;
  usb_audio_requested_volume = USB_AUDIO_DEFAULT_VOLUME;
  usb_audio_applied_volume = USB_AUDIO_DEFAULT_VOLUME;
  usb_audio_snapshot.sample_rate = USB_AUDIO_DEFAULT_SAMPLE_RATE;
  usb_audio_status = USB_AUDIO_OFF;
  usb_audio_output = USB_AUDIO_OUTPUT_WM8994;
  usb_audio_host_mode =
      (((saved_host & USB_AUDIO_HOST_BKP_MASK) == USB_AUDIO_HOST_BKP_MAGIC) &&
       ((saved_host & 0xFFU) == (uint32_t)USB_AUDIO_HOST_WINDOWS)) ?
      USB_AUDIO_HOST_WINDOWS : USB_AUDIO_HOST_LINUX;
  if ((saved_spdif & USB_AUDIO_SPDIF_BKP_MASK) ==
      USB_AUDIO_SPDIF_BKP_MAGIC)
  {
    const uint32_t saved_mode = saved_spdif & 0xFFU;
    if (saved_mode == (uint32_t)USB_AUDIO_SPDIF_UPSAMPLE_4X_HYBRID)
      usb_audio_spdif_mode = USB_AUDIO_SPDIF_UPSAMPLE_4X_HYBRID;
    else if (saved_mode == (uint32_t)USB_AUDIO_SPDIF_UPSAMPLE_4X_IIR)
      usb_audio_spdif_mode = USB_AUDIO_SPDIF_UPSAMPLE_4X_IIR;
    else if (saved_mode == (uint32_t)USB_AUDIO_SPDIF_UPSAMPLE_4X_TPDF)
      usb_audio_spdif_mode = USB_AUDIO_SPDIF_UPSAMPLE_4X_TPDF;
    else if (saved_mode == (uint32_t)USB_AUDIO_SPDIF_UPSAMPLE_4X_HEADROOM)
      usb_audio_spdif_mode = USB_AUDIO_SPDIF_UPSAMPLE_4X_HEADROOM;
    else if (saved_mode == (uint32_t)USB_AUDIO_SPDIF_REPEAT_4X)
      usb_audio_spdif_mode = USB_AUDIO_SPDIF_REPEAT_4X;
    else if (saved_mode == (uint32_t)USB_AUDIO_SPDIF_UPSAMPLE_4X)
      usb_audio_spdif_mode = USB_AUDIO_SPDIF_UPSAMPLE_4X;
    else
      usb_audio_spdif_mode = USB_AUDIO_SPDIF_NATIVE;
  }
  else
  {
    usb_audio_spdif_mode = USB_AUDIO_SPDIF_NATIVE;
  }
  usb_audio_snapshot.host_mode = usb_audio_host_mode;
  usb_audio_snapshot.spdif_mode = usb_audio_spdif_mode;
  usb_audio_prepared_output = USB_AUDIO_OUTPUT_WM8994;
  usb_audio_prepared_spdif_mode = usb_audio_spdif_mode;
  usb_audio_last_stats_publish_tick = 0U;
}

void USB_Audio_GetSnapshot(USB_AudioSnapshot *snapshot)
{
  if (snapshot == NULL) return;
  taskENTER_CRITICAL();
  *snapshot = usb_audio_snapshot;
  taskEXIT_CRITICAL();
}

void USB_Audio_RequestEnable(uint8_t enable)
{
  usb_audio_requested = (enable != 0U) ? 1U : 0U;
  if (enable != 0U) USB_MSC_RequestEnable(0U);
  USB_MSC_WakeManager();
  USB_Audio_Publish();
}

void USB_Audio_RequestVolume(uint8_t volume)
{
  uint8_t requested = (volume > 100U) ? 100U : volume;
  if (requested == usb_audio_requested_volume) return;

  usb_audio_requested_volume = requested;
  if (usb_audio_task_handle != NULL)
  {
    (void)xTaskNotify(usb_audio_task_handle, USB_AUDIO_NOTIFY_VOLUME, eSetBits);
  }
  USB_Audio_Publish();
}

void USB_Audio_RequestOutput(USB_AudioOutput output)
{
  USB_AudioOutput requested = (output == USB_AUDIO_OUTPUT_SPDIF) ?
                              USB_AUDIO_OUTPUT_SPDIF :
                              USB_AUDIO_OUTPUT_WM8994;
  if (requested == usb_audio_output) return;

  usb_audio_output = requested;
  if (AudioPlayer_IsBusy() != 0U) AudioPlayer_RequestStop();
  if (usb_audio_task_handle != NULL)
  {
    (void)xTaskNotify(usb_audio_task_handle, USB_AUDIO_NOTIFY_OUTPUT, eSetBits);
  }
  USB_Audio_Publish();
}

USB_AudioOutput USB_Audio_GetOutput(void)
{
  return usb_audio_output;
}

void USB_Audio_RequestHostMode(USB_AudioHostMode mode)
{
  const USB_AudioHostMode requested =
      (mode == USB_AUDIO_HOST_WINDOWS) ? USB_AUDIO_HOST_WINDOWS :
                                         USB_AUDIO_HOST_LINUX;
  if (requested == usb_audio_host_mode) return;

  usb_audio_host_mode = requested;
  HAL_PWR_EnableBkUpAccess();
  HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR1,
                      USB_AUDIO_HOST_BKP_MAGIC | (uint32_t)requested);

  /* The host mode changes the VID/PID and the complete Audio descriptor tree.
     Wake the USB manager so an active connection disconnects and enumerates
     again with the newly selected class version. */
  USB_MSC_WakeManager();
  USB_Audio_Publish();
}

USB_AudioHostMode USB_Audio_GetHostMode(void)
{
  return usb_audio_host_mode;
}

void USB_Audio_RequestSpdifMode(USB_AudioSpdifMode mode)
{
  USB_AudioSpdifMode requested = USB_AUDIO_SPDIF_NATIVE;
  if (mode == USB_AUDIO_SPDIF_UPSAMPLE_4X_HYBRID)
    requested = USB_AUDIO_SPDIF_UPSAMPLE_4X_HYBRID;
  else if (mode == USB_AUDIO_SPDIF_UPSAMPLE_4X_IIR)
    requested = USB_AUDIO_SPDIF_UPSAMPLE_4X_IIR;
  else if (mode == USB_AUDIO_SPDIF_UPSAMPLE_4X_TPDF)
    requested = USB_AUDIO_SPDIF_UPSAMPLE_4X_TPDF;
  else if (mode == USB_AUDIO_SPDIF_UPSAMPLE_4X_HEADROOM)
    requested = USB_AUDIO_SPDIF_UPSAMPLE_4X_HEADROOM;
  else if (mode == USB_AUDIO_SPDIF_REPEAT_4X)
    requested = USB_AUDIO_SPDIF_REPEAT_4X;
  else if (mode == USB_AUDIO_SPDIF_UPSAMPLE_4X)
    requested = USB_AUDIO_SPDIF_UPSAMPLE_4X;
  if (requested == usb_audio_spdif_mode) return;

  usb_audio_spdif_mode = requested;
  /* MUSIC shares the selected S/PDIF transport. Stop a current file cleanly
   * so its next start uses the newly selected output clock and formatter. */
  AudioPlayer_RequestStop();
  HAL_PWR_EnableBkUpAccess();
  HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR2,
                      USB_AUDIO_SPDIF_BKP_MAGIC | (uint32_t)requested);
  if (usb_audio_task_handle != NULL)
  {
    (void)xTaskNotify(usb_audio_task_handle, USB_AUDIO_NOTIFY_SPDIF_MODE,
                      eSetBits);
  }
  USB_Audio_Publish();
}

USB_AudioSpdifMode USB_Audio_GetSpdifMode(void)
{
  return usb_audio_spdif_mode;
}

uint8_t USB_Audio_EnableRequested(void)
{
  return usb_audio_requested;
}

uint8_t USB_Audio_IsActive(void)
{
  return usb_audio_active;
}

uint8_t USB_Audio_ClaimsCodec(void)
{
  return usb_audio_requested;
}

void USB_Audio_SetDeviceState(USB_AudioStatus status, uint8_t active,
                              uint8_t configured, uint8_t high_speed)
{
  usb_audio_status = status;
  usb_audio_active = active;
  usb_audio_configured = configured;
  usb_audio_high_speed = high_speed;
  USB_Audio_Publish();
}

static void USB_Audio_ResetStream(void)
{
  taskENTER_CRITICAL();
  usb_audio_write_frame = 0U;
  usb_audio_read_frame = 0U;
  usb_audio_dma_running = 0U;
  usb_audio_rebuffer_requested = 0U;
  usb_audio_feedback_integral_q16 = 0;
  usb_audio_feedback_filtered_queue_q8 =
      (int32_t)(USB_AUDIO_FEEDBACK_TARGET_FRAMES << 8U);
  usb_audio_spdif_status_frame = 0U;
  SPDIF_Upsampler4x_Reset(&usb_audio_spdif_upsampler);
  SPDIF_IirUpsampler4x_Reset(&usb_audio_spdif_iir_upsampler);
  SPDIF_HybridUpsampler4x_Reset(&usb_audio_spdif_hybrid_upsampler);
  taskEXIT_CRITICAL();
  memset(&usb_audio_dma, 0, sizeof(usb_audio_dma));
}

static void USB_Audio_StopPreparedHardware(void)
{
  if (usb_audio_prepared_output == USB_AUDIO_OUTPUT_SPDIF)
  {
    SPDIF_TX_Stop();
    SPDIF_TX_DeInit();
  }
  else
  {
    if (Touch_I2C_Reserve(500U) != 0U)
    {
      (void)BSP_AUDIO_OUT_Stop(CODEC_PDWN_SW);
      Touch_I2C_Release();
    }
    BSP_AUDIO_OUT_DeInit();
  }
}

static uint8_t USB_Audio_ConfigureHardware(uint32_t sample_rate)
{
  uint8_t result = AUDIO_ERROR;
  USB_AudioOutput output;

  if ((sample_rate != USB_AUDIO_RATE_44K) &&
      (sample_rate != USB_AUDIO_RATE_48K))
  {
    return 0U;
  }

  if (usb_audio_prepared != 0U)
  {
    usb_audio_prepared = 0U;
    usb_audio_dma_running = 0U;
    USB_Audio_StopPreparedHardware();
  }

  USB_Audio_ResetStream();
  output = usb_audio_output;
  if (output == USB_AUDIO_OUTPUT_SPDIF)
  {
    const uint8_t upsample =
        (usb_audio_spdif_mode != USB_AUDIO_SPDIF_NATIVE) ? 1U : 0U;
    const uint32_t output_rate =
        upsample != 0U ? sample_rate * USB_AUDIO_SPDIF_FACTOR : sample_rate;
    SPDIF_IirUpsampler4x_Init(&usb_audio_spdif_iir_upsampler, sample_rate);
    SPDIF_HybridUpsampler4x_Init(&usb_audio_spdif_hybrid_upsampler,
                                 sample_rate);
    result = (SPDIF_TX_Init(output_rate, 16U) != 0U) ?
             AUDIO_OK : AUDIO_ERROR;
  }
  else
  {
    if (Touch_I2C_Reserve(500U) == 0U)
    {
      return 0U;
    }
    result = BSP_AUDIO_OUT_InitEx(OUTPUT_DEVICE_HEADPHONE,
                                  usb_audio_requested_volume,
                                  sample_rate, 16U);
    if (result == AUDIO_OK)
    {
      BSP_AUDIO_OUT_SetAudioFrameSlot(CODEC_AUDIOFRAME_SLOT_02);
    }
    Touch_I2C_Release();
  }
  if (result != AUDIO_OK)
  {
    return 0U;
  }

  usb_audio_current_rate = sample_rate;
  usb_audio_applied_volume = usb_audio_requested_volume;
  usb_audio_applied_mute =
      (usb_audio_applied_volume == 0U) ? 1U : 0U;
  usb_audio_last_volume_apply_tick = xTaskGetTickCount();
  usb_audio_prepared_output = output;
  usb_audio_prepared_spdif_mode = usb_audio_spdif_mode;
  usb_audio_prepared = 1U;
  return 1U;
}

uint8_t USB_Audio_PrepareHardware(void)
{
  uint8_t result;

  USB_Audio_ResetStream();
  usb_audio_received_bytes = 0U;
  usb_audio_received_packets = 0U;
  usb_audio_underruns = 0U;
  usb_audio_overruns = 0U;
  usb_audio_muted = 0U;
  usb_audio_applied_mute =
      (usb_audio_requested_volume == 0U) ? 1U : 0U;
  usb_audio_requested_rate = USB_AUDIO_DEFAULT_SAMPLE_RATE;

  result = USB_Audio_ConfigureHardware(USB_AUDIO_DEFAULT_SAMPLE_RATE);
  return result;
}

void USB_Audio_ReleaseHardware(void)
{
  uint8_t was_prepared = usb_audio_prepared;
  usb_audio_prepared = 0U;
  usb_audio_dma_running = 0U;
  if (was_prepared != 0U) USB_Audio_StopPreparedHardware();
  USB_Audio_ResetStream();
}

static uint32_t USB_Audio_FillDmaHalf(uint8_t half)
{
  uint32_t read_frame = usb_audio_read_frame;
  uint32_t write_frame = usb_audio_write_frame;
  uint32_t available = write_frame - read_frame;
  uint32_t missing;

  if (usb_audio_prepared_output == USB_AUDIO_OUTPUT_SPDIF)
  {
    uint32_t *destination =
        &usb_audio_dma.spdif[(uint32_t)half *
                             USB_AUDIO_DMA_HALF_FRAMES * 2U];
    if (usb_audio_prepared_spdif_mode != USB_AUDIO_SPDIF_NATIVE)
    {
      const uint32_t output_rate =
          usb_audio_current_rate * USB_AUDIO_SPDIF_FACTOR;
      const uint32_t groups_needed =
          USB_AUDIO_DMA_HALF_FRAMES / USB_AUDIO_SPDIF_FACTOR;
      const uint32_t groups =
          (available < groups_needed) ? available : groups_needed;
      for (uint32_t group = 0U; group < groups; ++group)
      {
        const USB_AudioFrame source =
            usb_audio_input[(read_frame + group) & USB_AUDIO_INPUT_RING_MASK];
        int16_t interpolated[USB_AUDIO_SPDIF_FACTOR * 2U];
        if (usb_audio_prepared_spdif_mode == USB_AUDIO_SPDIF_REPEAT_4X)
        {
          for (uint32_t phase = 0U; phase < USB_AUDIO_SPDIF_FACTOR; ++phase)
          {
            interpolated[phase * 2U] = source.left;
            interpolated[phase * 2U + 1U] = source.right;
          }
        }
        else if (usb_audio_prepared_spdif_mode ==
                 USB_AUDIO_SPDIF_UPSAMPLE_4X_IIR)
        {
          SPDIF_IirUpsampler4x_Process(&usb_audio_spdif_iir_upsampler,
              source.left, source.right, interpolated);
        }
        else if (usb_audio_prepared_spdif_mode ==
                 USB_AUDIO_SPDIF_UPSAMPLE_4X_HYBRID)
        {
          SPDIF_HybridUpsampler4x_Process(
              &usb_audio_spdif_hybrid_upsampler, source.left, source.right,
              interpolated);
        }
        else
        {
          SPDIF_Upsampler4x_Process(&usb_audio_spdif_upsampler,
              source.left, source.right, interpolated,
              ((usb_audio_prepared_spdif_mode ==
                USB_AUDIO_SPDIF_UPSAMPLE_4X_HEADROOM) ||
               (usb_audio_prepared_spdif_mode ==
                USB_AUDIO_SPDIF_UPSAMPLE_4X_TPDF)) ? 1U : 0U,
              (usb_audio_prepared_spdif_mode ==
               USB_AUDIO_SPDIF_UPSAMPLE_4X_TPDF) ? 1U : 0U);
        }

        for (uint32_t phase = 0U; phase < USB_AUDIO_SPDIF_FACTOR; ++phase)
        {
          const uint32_t output_frame =
              group * USB_AUDIO_SPDIF_FACTOR + phase;
          destination[output_frame * 2U] =
              SPDIF_TX_EncodeSample(interpolated[phase * 2U], 16U,
                                    output_rate,
                                    usb_audio_spdif_status_frame);
          destination[output_frame * 2U + 1U] =
              SPDIF_TX_EncodeSample(interpolated[phase * 2U + 1U], 16U,
                                    output_rate,
                                    usb_audio_spdif_status_frame);
          usb_audio_spdif_status_frame =
              (usb_audio_spdif_status_frame + 1U) % 192U;
        }
      }
      read_frame += groups;
      missing = USB_AUDIO_DMA_HALF_FRAMES -
                groups * USB_AUDIO_SPDIF_FACTOR;

      /* Preserve valid IEC60958 channel-status bits while padding an
       * underrun with silence. Do not interpolate across a later track gap. */
      for (uint32_t frame = USB_AUDIO_DMA_HALF_FRAMES - missing;
           frame < USB_AUDIO_DMA_HALF_FRAMES; ++frame)
      {
        destination[frame * 2U] =
            SPDIF_TX_EncodeSample(0, 16U, output_rate,
                                  usb_audio_spdif_status_frame);
        destination[frame * 2U + 1U] =
            SPDIF_TX_EncodeSample(0, 16U, output_rate,
                                  usb_audio_spdif_status_frame);
        usb_audio_spdif_status_frame =
            (usb_audio_spdif_status_frame + 1U) % 192U;
      }
      if (missing != 0U)
      {
        SPDIF_Upsampler4x_Reset(&usb_audio_spdif_upsampler);
        SPDIF_IirUpsampler4x_Reset(&usb_audio_spdif_iir_upsampler);
        SPDIF_HybridUpsampler4x_Reset(&usb_audio_spdif_hybrid_upsampler);
      }
    }
    else
    {
      const uint32_t copied =
          (available < USB_AUDIO_DMA_HALF_FRAMES) ?
          available : USB_AUDIO_DMA_HALF_FRAMES;
      missing = USB_AUDIO_DMA_HALF_FRAMES - copied;
      for (uint32_t frame = 0U; frame < USB_AUDIO_DMA_HALF_FRAMES; ++frame)
      {
        int16_t left = 0;
        int16_t right = 0;
        if (frame < copied)
        {
          const USB_AudioFrame *source =
              &usb_audio_input[(read_frame + frame) &
                               USB_AUDIO_INPUT_RING_MASK];
          left = source->left;
          right = source->right;
        }
        destination[frame * 2U] =
            SPDIF_TX_EncodeSample(left, 16U, usb_audio_current_rate,
                                  usb_audio_spdif_status_frame);
        destination[frame * 2U + 1U] =
            SPDIF_TX_EncodeSample(right, 16U, usb_audio_current_rate,
                                  usb_audio_spdif_status_frame);
        usb_audio_spdif_status_frame =
            (usb_audio_spdif_status_frame + 1U) % 192U;
      }
      read_frame += copied;
    }
  }
  else
  {
    const uint32_t copied =
        (available < USB_AUDIO_DMA_HALF_FRAMES) ?
        available : USB_AUDIO_DMA_HALF_FRAMES;
    const uint32_t read_index = read_frame & USB_AUDIO_INPUT_RING_MASK;
    USB_AudioFrame *destination =
        &usb_audio_dma.pcm16[(uint32_t)half * USB_AUDIO_DMA_HALF_FRAMES];
    missing = USB_AUDIO_DMA_HALF_FRAMES - copied;
    uint32_t first = USB_AUDIO_INPUT_RING_FRAMES - read_index;
    if (first > copied) first = copied;
    if (first != 0U)
    {
      memcpy(destination, &usb_audio_input[read_index],
             first * sizeof(USB_AudioFrame));
    }
    if (copied > first)
    {
      memcpy(&destination[first], usb_audio_input,
             (copied - first) * sizeof(USB_AudioFrame));
    }
    if (missing != 0U)
    {
      memset(&destination[copied], 0,
             missing * sizeof(USB_AudioFrame));
    }
    read_frame += copied;
  }
  if (missing != 0U)
  {
    ++usb_audio_underruns;
  }

  __DMB();
  usb_audio_read_frame = read_frame;
  return missing;
}

static uint8_t USB_Audio_StopForRebuffer(void)
{
  uint8_t stopped = 0U;

  if (usb_audio_prepared_output == USB_AUDIO_OUTPUT_SPDIF)
  {
    SPDIF_TX_Stop();
    stopped = 1U;
  }
  else if (Touch_I2C_Reserve(50U) != 0U)
  {
    /* BSP stops SAI DMA before touching the codec, so the transport is
       stopped even if a subsequent mute register write reports an error. */
    if (BSP_AUDIO_OUT_Stop(CODEC_PDWN_SW) == AUDIO_OK)
      usb_audio_applied_mute = 1U;
    stopped = 1U;
    Touch_I2C_Release();
  }

  if (stopped != 0U)
  {
    usb_audio_dma_running = 0U;
    usb_audio_rebuffer_requested = 0U;
    usb_audio_feedback_integral_q16 = 0;
    usb_audio_feedback_filtered_queue_q8 =
        (int32_t)(USB_AUDIO_FEEDBACK_TARGET_FRAMES << 8U);
  }
  return stopped;
}

static void USB_Audio_ReceivePacket(const uint8_t *data, uint32_t size)
{
  uint32_t frames = size / USB_AUDIO_INPUT_FRAME_BYTES;
  uint32_t write_frame = usb_audio_write_frame;
  uint32_t read_frame = usb_audio_read_frame;
  uint32_t queued_before = write_frame - read_frame;
  uint32_t free_frames = USB_AUDIO_INPUT_RING_FRAMES -
                         queued_before;
  uint32_t accepted = frames;

  if ((data == NULL) || (frames == 0U) || (usb_audio_prepared == 0U)) return;
  if (accepted >= free_frames)
  {
    accepted = (free_frames > 1U) ? free_frames - 1U : 0U;
    ++usb_audio_overruns;
  }
  for (uint32_t frame = 0U; frame < accepted; ++frame)
  {
    const uint8_t *source = &data[frame * USB_AUDIO_INPUT_FRAME_BYTES];
    USB_AudioFrame *target =
        &usb_audio_input[(write_frame + frame) & USB_AUDIO_INPUT_RING_MASK];
    target->left = (int16_t)((uint16_t)source[0] |
                             ((uint16_t)source[1] << 8));
    target->right = (int16_t)((uint16_t)source[2] |
                              ((uint16_t)source[3] << 8));
  }
  __DMB();
  usb_audio_write_frame = write_frame + accepted;
  usb_audio_received_bytes += accepted * USB_AUDIO_INPUT_FRAME_BYTES;
  ++usb_audio_received_packets;
  /* Packet callbacks run every USB microframe. Wake the high-priority audio
     task only once when the initial prebuffer crosses its start watermark;
     SAI DMA half/full callbacks drive all steady-state refills. */
  if ((usb_audio_task_handle != NULL) &&
      (usb_audio_dma_running == 0U) &&
      (queued_before < USB_AUDIO_START_FRAMES) &&
      ((queued_before + accepted) >= USB_AUDIO_START_FRAMES))
  {
    BaseType_t wake = pdFALSE;
    (void)xTaskNotifyFromISR(usb_audio_task_handle, USB_AUDIO_NOTIFY_DATA,
                             eSetBits, &wake);
    portYIELD_FROM_ISR(wake);
  }
}

uint8_t USB_Audio_DmaIsRunning(void)
{
  return usb_audio_dma_running;
}

static void USB_Audio_NotifyDma(uint32_t bit)
{
  if (usb_audio_task_handle != NULL)
  {
    BaseType_t wake = pdFALSE;
    (void)xTaskNotifyFromISR(usb_audio_task_handle, bit, eSetBits, &wake);
    portYIELD_FROM_ISR(wake);
  }
}

void USB_Audio_DmaHalfCallback(void)
{
  USB_Audio_NotifyDma(USB_AUDIO_NOTIFY_DMA_HALF);
}

void USB_Audio_DmaFullCallback(void)
{
  USB_Audio_NotifyDma(USB_AUDIO_NOTIFY_DMA_FULL);
}

void USB_Audio_DmaErrorCallback(void)
{
  USB_Audio_NotifyDma(USB_AUDIO_NOTIFY_ERROR);
}

void USB_Audio_Task(void const *argument)
{
  uint32_t notify_bits;
  TickType_t wait_ticks = pdMS_TO_TICKS(100U);
  (void)argument;
  usb_audio_task_handle = xTaskGetCurrentTaskHandle();

  for (;;)
  {
    notify_bits = 0U;
    (void)xTaskNotifyWait(0U, 0xFFFFFFFFUL, &notify_bits,
                          wait_ticks);
    wait_ticks = pdMS_TO_TICKS(100U);
    if ((usb_audio_active != 0U) && (usb_audio_prepared != 0U))
    {
      if ((usb_audio_requested_rate != usb_audio_current_rate) ||
          (usb_audio_output != usb_audio_prepared_output) ||
          ((usb_audio_output == USB_AUDIO_OUTPUT_SPDIF) &&
           ((usb_audio_spdif_mode != usb_audio_prepared_spdif_mode) ||
            ((notify_bits & USB_AUDIO_NOTIFY_SPDIF_MODE) != 0U))) ||
          ((notify_bits & USB_AUDIO_NOTIFY_OUTPUT) != 0U))
      {
        uint32_t new_rate = usb_audio_requested_rate;
        if (USB_Audio_ConfigureHardware(new_rate) == 0U)
        {
          usb_audio_status = USB_AUDIO_ERROR;
          USB_Audio_Publish();
          continue;
        }
        /* The old DMA notifications belong to the stopped stream. */
        notify_bits &= ~(USB_AUDIO_NOTIFY_DMA_HALF |
                         USB_AUDIO_NOTIFY_DMA_FULL |
                         USB_AUDIO_NOTIFY_ERROR |
                         USB_AUDIO_NOTIFY_SPDIF_MODE);
      }

      if ((usb_audio_prepared_output == USB_AUDIO_OUTPUT_WM8994) &&
          (usb_audio_requested_volume != usb_audio_applied_volume))
      {
        const TickType_t minimum_ticks =
            pdMS_TO_TICKS(USB_AUDIO_VOLUME_APPLY_MS);
        const TickType_t now = xTaskGetTickCount();
        const TickType_t elapsed = now - usb_audio_last_volume_apply_tick;
        if (elapsed >= minimum_ticks)
        {
          const uint8_t target_volume = usb_audio_requested_volume;
          if (Touch_I2C_Reserve(20U) != 0U)
          {
            if (BSP_AUDIO_OUT_SetVolume(target_volume) == AUDIO_OK)
            {
              usb_audio_applied_volume = target_volume;
              /* SetVolume(0) mutes; every non-zero value unmutes. Reconcile
                 the independent USB mute state immediately below. */
              usb_audio_applied_mute =
                  (target_volume == 0U) ? 1U : 0U;
              usb_audio_last_volume_apply_tick = now;
            }
            Touch_I2C_Release();
          }
        }
        else
        {
          wait_ticks = minimum_ticks - elapsed;
        }
      }

      const uint8_t target_mute =
          ((usb_audio_muted != 0U) ||
           (usb_audio_requested_volume == 0U)) ? 1U : 0U;
      if ((usb_audio_prepared_output == USB_AUDIO_OUTPUT_WM8994) &&
          (target_mute != usb_audio_applied_mute))
      {
        if (Touch_I2C_Reserve(20U) != 0U)
        {
          if (BSP_AUDIO_OUT_SetMute(target_mute) == AUDIO_OK)
            usb_audio_applied_mute = target_mute;
          Touch_I2C_Release();
        }
      }
      if ((notify_bits & USB_AUDIO_NOTIFY_ERROR) != 0U)
      {
        if (usb_audio_prepared_output == USB_AUDIO_OUTPUT_SPDIF)
          SPDIF_TX_Stop();
        else if (Touch_I2C_Reserve(20U) != 0U)
        {
          (void)BSP_AUDIO_OUT_SetMute(AUDIO_MUTE_ON);
          Touch_I2C_Release();
        }
        usb_audio_dma_running = 0U;
        usb_audio_rebuffer_requested = 0U;
        ++usb_audio_underruns;
      }
      if ((usb_audio_rebuffer_requested != 0U) &&
          (usb_audio_dma_running != 0U))
      {
        (void)USB_Audio_StopForRebuffer();
      }
      if ((usb_audio_dma_running == 0U) &&
          ((usb_audio_write_frame - usb_audio_read_frame) >=
           USB_AUDIO_START_FRAMES))
      {
        (void)USB_Audio_FillDmaHalf(0U);
        (void)USB_Audio_FillDmaHalf(1U);
        uint8_t play_result = AUDIO_ERROR;
        uint32_t play_bytes;
        if (usb_audio_prepared_output == USB_AUDIO_OUTPUT_SPDIF)
        {
          play_bytes = sizeof(usb_audio_dma.spdif);
          play_result = (SPDIF_TX_Start(usb_audio_dma.spdif,
                                        play_bytes) != 0U) ?
                        AUDIO_OK : AUDIO_ERROR;
        }
        else if (Touch_I2C_Reserve(500U) != 0U)
        {
          play_bytes = sizeof(usb_audio_dma.pcm16);
          play_result = BSP_AUDIO_OUT_Play((uint16_t *)usb_audio_dma.pcm16,
                                            play_bytes);
          if (play_result == AUDIO_OK)
          {
            /* The WM8994 BSP Play hook always unmutes. Restore a host mute or
               a zero-gain hardware mute before releasing the codec bus. */
            usb_audio_applied_mute = 0U;
            if ((target_mute != 0U) &&
                (BSP_AUDIO_OUT_SetMute(target_mute) == AUDIO_OK))
            {
              usb_audio_applied_mute = target_mute;
            }
          }
          Touch_I2C_Release();
        }
        if (play_result == AUDIO_OK)
        {
          usb_audio_dma_running = 1U;
        }
        else
        {
          if (usb_audio_prepared_output == USB_AUDIO_OUTPUT_SPDIF)
            SPDIF_TX_Stop();
          ++usb_audio_underruns;
        }
      }
      else if (usb_audio_dma_running != 0U)
      {
        uint32_t missing = 0U;
        if ((notify_bits & USB_AUDIO_NOTIFY_DMA_HALF) != 0U)
          missing = USB_Audio_FillDmaHalf(0U);
        if ((missing == 0U) &&
            ((notify_bits & USB_AUDIO_NOTIFY_DMA_FULL) != 0U))
          missing = USB_Audio_FillDmaHalf(1U);
        if (missing != 0U)
        {
          /* Do not keep consuming one USB packet ahead forever after a track
             gap. Stop at this DMA boundary and restore the 8192-frame start
             waterline before playing again. */
          usb_audio_rebuffer_requested = 1U;
          (void)USB_Audio_StopForRebuffer();
        }
      }
    }

    USB_Audio_Publish();
  }
}

/* ST USB Audio Class callbacks. They run in USB interrupt context, so codec
 * I2C and SAI start/stop operations are deliberately handled by RTOS tasks. */
static int8_t USB_Audio_InterfaceInit(uint32_t frequency, uint32_t volume,
                                      uint32_t options)
{
  if ((frequency == USB_AUDIO_RATE_44K) ||
      (frequency == USB_AUDIO_RATE_48K))
  {
    usb_audio_requested_rate = frequency;
  }
  (void)volume; (void)options;
  return (usb_audio_prepared != 0U) ? 0 : -1;
}

static int8_t USB_Audio_InterfaceDeInit(uint32_t options)
{
  (void)options;
  return 0;
}

static int8_t USB_Audio_InterfaceCommand(uint8_t *buffer, uint32_t size,
                                         uint8_t command)
{
  (void)buffer; (void)size; (void)command;
  return 0;
}

static int8_t USB_Audio_InterfaceVolume(uint8_t volume)
{
  (void)volume;
  return 0;
}

static int8_t USB_Audio_InterfaceMute(uint8_t command)
{
  usb_audio_muted = (command != 0U) ? 1U : 0U;
  if (usb_audio_task_handle != NULL)
  {
    BaseType_t wake = pdFALSE;
    (void)xTaskNotifyFromISR(usb_audio_task_handle, USB_AUDIO_NOTIFY_DATA,
                             eSetBits, &wake);
    portYIELD_FROM_ISR(wake);
  }
  return 0;
}

static int8_t USB_Audio_InterfaceFrequency(uint32_t frequency)
{
  uint32_t previous;

  if ((frequency != USB_AUDIO_RATE_44K) &&
      (frequency != USB_AUDIO_RATE_48K))
  {
    return -1;
  }

  previous = usb_audio_requested_rate;
  if (frequency == previous)
  {
    /* Hosts may repeat SET_CUR while the stream is active. Waking the audio
       task cannot change the hardware and adds
       avoidable scheduling work during the accompanying isochronous gap. */
    return 0;
  }

  usb_audio_requested_rate = frequency;
  USB_Audio_NotifyDma(USB_AUDIO_NOTIFY_RATE);
  return 0;
}

static int8_t USB_Audio_InterfacePeriodic(uint8_t *buffer, uint32_t size,
                                          uint8_t command)
{
  if (command == AUDIO_OUT_TC) USB_Audio_ReceivePacket(buffer, size);
  return 0;
}

static int8_t USB_Audio_InterfaceGetState(void)
{
  return 0;
}

static uint32_t USB_Audio_InterfaceGetFeedback(void)
{
  const uint32_t sample_rate = usb_audio_current_rate;
  const uint32_t nominal_q16 = sample_rate << 16U;
  int32_t filtered_q8;
  int32_t queue_q8;
  int32_t error_frames;
  int32_t integral_q16;
  int64_t correction_q16;
  int64_t correction_limit_q16;
  int64_t integral_limit_q16;
  uint32_t queued;

  /* Until SAI DMA is consuming real samples there is no meaningful waterline
     error. Advertise the nominal rate and start the controller from center. */
  if ((usb_audio_prepared == 0U) || (usb_audio_dma_running == 0U))
  {
    usb_audio_feedback_integral_q16 = 0;
    usb_audio_feedback_filtered_queue_q8 =
        (int32_t)(USB_AUDIO_FEEDBACK_TARGET_FRAMES << 8U);
    return nominal_q16;
  }

  queued = usb_audio_write_frame - usb_audio_read_frame;
  if (queued >= USB_AUDIO_INPUT_RING_FRAMES)
  {
    queued = USB_AUDIO_INPUT_RING_FRAMES - 1U;
  }

  /* Smooth the native 2048-frame or 4x-mode 512-frame DMA refill steps before
     applying the PI loop. This loop changes only the host packet cadence;
     PLLI2S/SAI remains untouched. */
  queue_q8 = (int32_t)(queued << 8U);
  filtered_q8 = usb_audio_feedback_filtered_queue_q8;
  filtered_q8 += (queue_q8 - filtered_q8) >>
                 USB_AUDIO_FEEDBACK_FILTER_SHIFT;
  usb_audio_feedback_filtered_queue_q8 = filtered_q8;
  error_frames = (int32_t)USB_AUDIO_FEEDBACK_TARGET_FRAMES -
                 (filtered_q8 >> 8U);

  integral_limit_q16 =
      ((int64_t)sample_rate * USB_AUDIO_FEEDBACK_I_MAX_PPM * 65536LL) /
      1000000LL;
  integral_q16 = usb_audio_feedback_integral_q16;
  integral_q16 += error_frames;
  if ((int64_t)integral_q16 > integral_limit_q16)
    integral_q16 = (int32_t)integral_limit_q16;
  else if ((int64_t)integral_q16 < -integral_limit_q16)
    integral_q16 = (int32_t)-integral_limit_q16;
  usb_audio_feedback_integral_q16 = integral_q16;

  correction_q16 = ((int64_t)error_frames * USB_AUDIO_FEEDBACK_P_Q16) +
                   integral_q16;
  correction_limit_q16 =
      ((int64_t)sample_rate * USB_AUDIO_FEEDBACK_MAX_PPM * 65536LL) /
      1000000LL;
  if (correction_q16 > correction_limit_q16)
    correction_q16 = correction_limit_q16;
  else if (correction_q16 < -correction_limit_q16)
    correction_q16 = -correction_limit_q16;

  return (uint32_t)((int64_t)nominal_q16 + correction_q16);
}

USBD_AUDIO_ItfTypeDef USBD_AUDIO_fops =
{
  USB_Audio_InterfaceInit,
  USB_Audio_InterfaceDeInit,
  USB_Audio_InterfaceCommand,
  USB_Audio_InterfaceVolume,
  USB_Audio_InterfaceMute,
  USB_Audio_InterfaceFrequency,
  USB_Audio_InterfacePeriodic,
  USB_Audio_InterfaceGetState,
  USB_Audio_InterfaceGetFeedback
};
