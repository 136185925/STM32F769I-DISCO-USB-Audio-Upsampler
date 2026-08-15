#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum
{
  AUDIO_PLAYER_IDLE = 0,
  AUDIO_PLAYER_LOADING,
  AUDIO_PLAYER_PLAYING,
  AUDIO_PLAYER_PAUSED,
  AUDIO_PLAYER_FINISHED,
  AUDIO_PLAYER_NO_CARD,
  AUDIO_PLAYER_UNSUPPORTED,
  AUDIO_PLAYER_ERROR,
  AUDIO_PLAYER_BUSY
} AudioPlayerStatus;

typedef struct
{
  AudioPlayerStatus status;
  char filename[13];
  uint8_t volume;
  uint8_t channels;
  uint8_t last_fatfs_result;
  uint8_t bits_per_sample;
  uint32_t sample_rate;
  uint32_t data_bytes;
  uint32_t position_bytes;
  uint32_t duration_ms;
  uint32_t position_ms;
  uint32_t generation;
} AudioPlayerSnapshot;

extern volatile uint32_t audio_player_generation;
extern volatile uint32_t audio_player_dma_half_count;
extern volatile uint32_t audio_player_dma_full_count;
extern volatile uint32_t audio_player_refill_count;
extern volatile uint32_t audio_player_error_count;
extern volatile uint8_t audio_player_debug_command;

void AudioPlayer_CreateResources(void);
void AudioPlayer_Task(void const *argument);
void AudioPlayer_GetSnapshot(AudioPlayerSnapshot *snapshot);
uint8_t AudioPlayer_IsBusy(void);
void AudioPlayer_RequestPlay(const char *filename);
void AudioPlayer_RequestTogglePause(void);
void AudioPlayer_RequestSeek(uint16_t permille);
void AudioPlayer_RequestVolume(uint8_t volume);
void AudioPlayer_RequestStop(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_PLAYER_H */
