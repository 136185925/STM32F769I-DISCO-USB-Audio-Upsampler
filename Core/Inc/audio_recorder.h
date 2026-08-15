#ifndef AUDIO_RECORDER_H
#define AUDIO_RECORDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum
{
  AUDIO_RECORDER_READY = 0,
  AUDIO_RECORDER_STARTING,
  AUDIO_RECORDER_RECORDING,
  AUDIO_RECORDER_STOPPING,
  AUDIO_RECORDER_NO_CARD,
  AUDIO_RECORDER_FILESYSTEM_ERROR,
  AUDIO_RECORDER_AUDIO_ERROR
} AudioRecorderStatus;

typedef struct
{
  AudioRecorderStatus status;
  uint32_t generation;
  uint32_t data_bytes;
  uint32_t elapsed_seconds;
  uint32_t dropped_chunks;
  uint16_t peak;
  uint16_t codec_id;
  uint8_t codec_ready;
  uint8_t last_fatfs_result;
  char filename[13];
} AudioRecorderSnapshot;

extern volatile uint32_t audio_recorder_dma_half_count;
extern volatile uint32_t audio_recorder_dma_full_count;
extern volatile uint32_t audio_recorder_write_count;
extern volatile uint32_t audio_recorder_error_count;
/* Debugger-accessible test command: 1=start, 2=stop, automatically clears. */
extern volatile uint8_t audio_recorder_debug_command;

void AudioRecorder_CreateResources(void);
void AudioRecorder_Task(void const *argument);
void AudioRecorder_RequestStart(void);
void AudioRecorder_RequestStop(void);
void AudioRecorder_GetSnapshot(AudioRecorderSnapshot *snapshot);
uint8_t AudioRecorder_IsBusy(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_RECORDER_H */
