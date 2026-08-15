#ifndef AUDIO_SPECTRUM_H
#define AUDIO_SPECTRUM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define AUDIO_SPECTRUM_BAND_COUNT 80U

typedef enum
{
  AUDIO_SPECTRUM_STOPPED = 0,
  AUDIO_SPECTRUM_STARTING,
  AUDIO_SPECTRUM_RUNNING,
  AUDIO_SPECTRUM_BUSY,
  AUDIO_SPECTRUM_ERROR
} AudioSpectrumStatus;

typedef struct
{
  AudioSpectrumStatus status;
  uint32_t generation;
  uint32_t sample_rate;
  uint32_t peak_frequency_hz;
  int16_t peak_db_tenths;
  uint16_t dropped_frames;
  uint32_t trigger_frequency_hz;
  int16_t trigger_threshold_db_tenths;
  int16_t trigger_level_db_tenths;
  uint32_t stopwatch_ticks;
  uint32_t trigger_count;
  uint8_t stopwatch_running;
  uint8_t trigger_armed;
  int16_t bands_db_tenths[AUDIO_SPECTRUM_BAND_COUNT];
} AudioSpectrumSnapshot;

extern volatile uint32_t audio_spectrum_fft_count;
extern volatile uint32_t audio_spectrum_goertzel_count;
extern volatile uint32_t audio_spectrum_dma_half_count;
extern volatile uint32_t audio_spectrum_dma_full_count;
extern volatile uint32_t audio_spectrum_error_count;
extern volatile uint32_t audio_spectrum_trigger_queue_drop_count;
extern volatile uint32_t audio_spectrum_fft_queue_drop_count;

void AudioSpectrum_CreateResources(void);
void AudioSpectrum_Task(void const *argument);
void AudioSpectrum_TriggerTask(void const *argument);
void AudioSpectrum_RequestStart(void);
void AudioSpectrum_RequestStop(void);
void AudioSpectrum_GetSnapshot(AudioSpectrumSnapshot *snapshot);
void AudioSpectrum_SetTriggerFrequency(uint32_t frequency_hz);
void AudioSpectrum_SetTriggerThreshold(int16_t threshold_db_tenths);
void AudioSpectrum_ResetTriggerStopwatch(void);
uint8_t AudioSpectrum_IsBusy(void);
void AudioSpectrum_HalfTransferFromISR(void);
void AudioSpectrum_TransferCompleteFromISR(void);
void AudioSpectrum_ErrorFromISR(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_SPECTRUM_H */
