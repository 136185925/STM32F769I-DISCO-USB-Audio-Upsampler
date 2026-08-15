#ifndef AUDIO_DIAG_LOG_H
#define AUDIO_DIAG_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ff.h"
#include <stdint.h>

typedef enum
{
  AUDIO_DIAG_BOOT = 0,
  AUDIO_DIAG_ENABLE_REQUEST,
  AUDIO_DIAG_DEVICE_STATE,
  AUDIO_DIAG_SESSION_PREPARE,
  AUDIO_DIAG_SESSION_RELEASE,
  AUDIO_DIAG_RECONFIG_BEGIN,
  AUDIO_DIAG_RECONFIG_OK,
  AUDIO_DIAG_RECONFIG_FAIL,
  AUDIO_DIAG_RATE_REQUEST,
  AUDIO_DIAG_OUTPUT_REQUEST,
  AUDIO_DIAG_DMA_START,
  AUDIO_DIAG_DMA_START_FAIL,
  AUDIO_DIAG_DMA_UNDERRUN,
  AUDIO_DIAG_REBUFFER_BEGIN,
  AUDIO_DIAG_DMA_ERROR,
  AUDIO_DIAG_RING_OVERRUN,
  AUDIO_DIAG_FEEDBACK_ERROR,
  AUDIO_DIAG_STATUS
} AudioDiagEvent;

/* This function is safe in both task and interrupt context. It only copies a
 * fixed-size record into an SRAM ring and never calls FatFs. */
void AudioDiag_Log(AudioDiagEvent event, uint32_t sample_rate,
                   uint8_t output, uint8_t dma_running, uint8_t high_speed,
                   uint32_t queued_frames, uint32_t underruns,
                   uint32_t overruns, uint32_t feedback_q16,
                   uint32_t feedback_tx, uint32_t feedback_errors,
                   uint32_t arg0, uint32_t arg1);

/* Called only by SD_Storage_Task after the volume is mounted. Records are
 * appended to 0:/USBXRUN.CSV in bounded batches and retained on I/O errors. */
FRESULT AudioDiag_Service(void);

extern volatile uint32_t audio_diag_dropped_count;
extern volatile uint32_t audio_diag_write_error_count;
extern volatile uint32_t audio_diag_written_record_count;

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_DIAG_LOG_H */
