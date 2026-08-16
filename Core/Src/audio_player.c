#include "audio_player.h"

#include "audio_recorder.h"
#include "audio_spectrum.h"
#include "fatfs.h"
#include "sdmmc.h"
#include "spdif_iir_upsampler.h"
#include "spdif_tx.h"
#include "spdif_upsampler.h"
#include "touch.h"
#include "usb_audio.h"
#include "stm32f769i_discovery_audio.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include <string.h>
#include <stdio.h>

#define PLAYER_DEFAULT_VOLUME       20U
#define PLAYER_DMA_BYTES            32768U
#define PLAYER_HALF_BYTES           (PLAYER_DMA_BYTES / 2U)
#define PLAYER_COMMAND_QUEUE_LENGTH 8U
#define PLAYER_NOTIFY_HALF          (1UL << 0)
#define PLAYER_NOTIFY_FULL          (1UL << 1)
#define PLAYER_NOTIFY_COMMAND       (1UL << 2)
#define PLAYER_SPDIF_FACTOR         SPDIF_UPSAMPLER_FACTOR
#define PLAYER_UPSAMPLE_INPUT_FRAMES \
    (PLAYER_HALF_BYTES / (8U * PLAYER_SPDIF_FACTOR))

#if ((PLAYER_HALF_BYTES / 8U) % PLAYER_SPDIF_FACTOR) != 0U
#error "Player DMA half-buffer must contain complete 4x S/PDIF groups"
#endif

typedef enum
{
  PLAYER_COMMAND_PLAY = 1,
  PLAYER_COMMAND_TOGGLE,
  PLAYER_COMMAND_SEEK,
  PLAYER_COMMAND_VOLUME,
  PLAYER_COMMAND_STOP
} PlayerCommandType;

typedef struct
{
  uint8_t type;
  uint16_t value;
  char filename[13];
} PlayerCommand;

typedef struct
{
  uint16_t channels;
  uint16_t bits_per_sample;
  uint16_t block_align;
  uint32_t sample_rate;
  uint32_t byte_rate;
  uint32_t data_offset;
  uint32_t data_bytes;
} PlayerWaveInfo;

volatile uint32_t audio_player_generation = 0U;
volatile uint32_t audio_player_dma_half_count = 0U;
volatile uint32_t audio_player_dma_full_count = 0U;
volatile uint32_t audio_player_refill_count = 0U;
volatile uint32_t audio_player_error_count = 0U;
volatile uint8_t audio_player_debug_command = 0U;

__attribute__((section(".dma_buffer"), aligned(32)))
static uint8_t player_dma_buffer[PLAYER_DMA_BYTES];

static QueueHandle_t player_command_queue;
static TaskHandle_t player_task_handle;
static AudioPlayerSnapshot player_snapshot;
static volatile uint8_t player_active;
static uint8_t player_volume = PLAYER_DEFAULT_VOLUME;
static USB_AudioOutput player_output = USB_AUDIO_OUTPUT_WM8994;
static uint8_t player_spdif_upsample;
static uint8_t player_spdif_headroom;
static uint8_t player_spdif_repeat;
static uint8_t player_spdif_tpdf;
static uint8_t player_spdif_iir;
static uint8_t player_spdif_hybrid;
static uint8_t player_spdif_hybrid_ns2;
static uint8_t player_spdif_minphase_ns2;
static uint8_t player_spdif_minphase_ns5;
static uint32_t player_spdif_status_frame;
static SPDIF_Upsampler4x player_spdif_upsampler;
static SPDIF_IirUpsampler4x player_spdif_iir_upsampler;
static SPDIF_HybridUpsampler4x player_spdif_hybrid_upsampler;
static uint16_t player_spdif_tail_frames;
static uint8_t player_last_fill_output_valid;

/* Cached CPU staging prevents forward filter processing from overwriting the
 * compact WAV frames while the DMA destination expands to 32 bytes per input
 * frame. 4x mode is limited to 16-bit mono/stereo, hence four bytes/frame. */
static uint8_t player_upsample_input[PLAYER_UPSAMPLE_INPUT_FRAMES * 4U];

static uint16_t Player_ReadLe16(const uint8_t *data)
{
  return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t Player_ReadLe32(const uint8_t *data)
{
  return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
         ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static int32_t Player_ReadLe24Signed(const uint8_t *data)
{
  uint32_t value = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                   ((uint32_t)data[2] << 16);
  if ((value & 0x00800000UL) != 0U)
  {
    value |= 0xFF000000UL;
  }
  return (int32_t)value;
}

static void Player_Publish(AudioPlayerSnapshot *snapshot)
{
  taskENTER_CRITICAL();
  snapshot->generation = ++audio_player_generation;
  player_snapshot = *snapshot;
  taskEXIT_CRITICAL();
}

static void Player_UpdatePosition(AudioPlayerSnapshot *snapshot, uint32_t position_bytes)
{
  if (position_bytes > snapshot->data_bytes)
  {
    position_bytes = snapshot->data_bytes;
  }
  snapshot->position_bytes = position_bytes;
  snapshot->position_ms = (snapshot->data_bytes == 0U) ? 0U :
      (uint32_t)(((uint64_t)position_bytes * snapshot->duration_ms) /
                 snapshot->data_bytes);
  taskENTER_CRITICAL();
  player_snapshot.position_bytes = snapshot->position_bytes;
  player_snapshot.position_ms = snapshot->position_ms;
  taskEXIT_CRITICAL();
}

void AudioPlayer_GetSnapshot(AudioPlayerSnapshot *snapshot)
{
  if (snapshot == NULL)
  {
    return;
  }
  taskENTER_CRITICAL();
  *snapshot = player_snapshot;
  taskEXIT_CRITICAL();
}

uint8_t AudioPlayer_IsBusy(void)
{
  return player_active;
}

void AudioPlayer_CreateResources(void)
{
  memset(&player_snapshot, 0, sizeof(player_snapshot));
  player_snapshot.status = AUDIO_PLAYER_IDLE;
  player_snapshot.volume = PLAYER_DEFAULT_VOLUME;
  player_volume = PLAYER_DEFAULT_VOLUME;
  player_command_queue = xQueueCreate(PLAYER_COMMAND_QUEUE_LENGTH,
                                      sizeof(PlayerCommand));
}

static void Player_SendCommand(PlayerCommand *command)
{
  if ((player_command_queue != NULL) &&
      (xQueueSend(player_command_queue, command, pdMS_TO_TICKS(20U)) == pdPASS))
  {
    if (player_task_handle != NULL)
    {
      (void)xTaskNotify(player_task_handle, PLAYER_NOTIFY_COMMAND, eSetBits);
    }
  }
  else
  {
    ++audio_player_error_count;
  }
}

void AudioPlayer_RequestPlay(const char *filename)
{
  if ((SD_Storage_ApplicationAccessAllowed() == 0U) ||
      (USB_Audio_ClaimsCodec() != 0U)) return;
  PlayerCommand command;
  if (filename == NULL) return;
  memset(&command, 0, sizeof(command));
  command.type = PLAYER_COMMAND_PLAY;
  strncpy(command.filename, filename, sizeof(command.filename) - 1U);
  Player_SendCommand(&command);
}

void AudioPlayer_RequestTogglePause(void)
{
  PlayerCommand command = {0};
  command.type = PLAYER_COMMAND_TOGGLE;
  Player_SendCommand(&command);
}

void AudioPlayer_RequestSeek(uint16_t permille)
{
  PlayerCommand command = {0};
  command.type = PLAYER_COMMAND_SEEK;
  command.value = permille > 1000U ? 1000U : permille;
  Player_SendCommand(&command);
}

void AudioPlayer_RequestVolume(uint8_t volume)
{
  PlayerCommand command = {0};
  command.type = PLAYER_COMMAND_VOLUME;
  command.value = volume > 100U ? 100U : volume;
  Player_SendCommand(&command);
}

void AudioPlayer_RequestStop(void)
{
  PlayerCommand command = {0};
  command.type = PLAYER_COMMAND_STOP;
  Player_SendCommand(&command);
}

static FRESULT Player_ReadExact(FIL *file, void *buffer, UINT size)
{
  UINT read = 0U;
  FRESULT result = f_read(file, buffer, size, &read);
  return ((result == FR_OK) && (read == size)) ? FR_OK :
         ((result == FR_OK) ? FR_INVALID_OBJECT : result);
}

static uint8_t Player_IsSupportedRate(uint32_t rate)
{
  return ((rate == 8000U) || (rate == 11025U) || (rate == 16000U) ||
          (rate == 22050U) || (rate == 32000U) || (rate == 44100U) ||
          (rate == 48000U) || (rate == 96000U)) ? 1U : 0U;
}

static FRESULT Player_ParseWave(FIL *file, PlayerWaveInfo *info)
{
  uint8_t riff[12];
  uint8_t chunk[8];
  uint8_t fmt[40];
  static const uint8_t pcm_subformat[16] =
      {0x01U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x10U, 0x00U,
       0x80U, 0x00U, 0x00U, 0xAAU, 0x00U, 0x38U, 0x9BU, 0x71U};
  uint8_t fmt_found = 0U;
  uint8_t data_found = 0U;
  FRESULT result;

  memset(info, 0, sizeof(*info));
  result = Player_ReadExact(file, riff, sizeof(riff));
  if (result != FR_OK) return result;
  if ((memcmp(&riff[0], "RIFF", 4U) != 0) ||
      (memcmp(&riff[8], "WAVE", 4U) != 0))
  {
    return FR_INVALID_OBJECT;
  }

  while (f_tell(file) + 8U <= f_size(file))
  {
    uint32_t chunk_size;
    uint32_t chunk_data;
    result = Player_ReadExact(file, chunk, sizeof(chunk));
    if (result != FR_OK) return result;
    chunk_size = Player_ReadLe32(&chunk[4]);
    chunk_data = f_tell(file);
    if (memcmp(chunk, "fmt ", 4U) == 0)
    {
      uint32_t fmt_bytes = chunk_size < sizeof(fmt) ? chunk_size : sizeof(fmt);
      uint16_t format_tag;
      uint16_t bits_per_sample;
      if (chunk_size < 16U) return FR_INVALID_OBJECT;
      memset(fmt, 0, sizeof(fmt));
      result = Player_ReadExact(file, fmt, fmt_bytes);
      if (result != FR_OK) return result;
      format_tag = Player_ReadLe16(&fmt[0]);
      bits_per_sample = Player_ReadLe16(&fmt[14]);
      if (((format_tag != 1U) &&
           ((format_tag != 0xFFFEU) || (chunk_size < 40U) ||
            (Player_ReadLe16(&fmt[16]) < 22U) ||
            (Player_ReadLe16(&fmt[18]) != bits_per_sample) ||
            (memcmp(&fmt[24], pcm_subformat, sizeof(pcm_subformat)) != 0))) ||
          ((Player_ReadLe16(&fmt[2]) != 1U) && (Player_ReadLe16(&fmt[2]) != 2U)) ||
          ((bits_per_sample != 16U) && (bits_per_sample != 24U)))
      {
        return FR_INVALID_PARAMETER;
      }
      info->channels = Player_ReadLe16(&fmt[2]);
      info->bits_per_sample = bits_per_sample;
      info->sample_rate = Player_ReadLe32(&fmt[4]);
      info->byte_rate = Player_ReadLe32(&fmt[8]);
      info->block_align = Player_ReadLe16(&fmt[12]);
      fmt_found = 1U;
    }
    else if (memcmp(chunk, "data", 4U) == 0)
    {
      info->data_offset = chunk_data;
      info->data_bytes = chunk_size;
      if (info->data_offset + info->data_bytes > f_size(file))
      {
        info->data_bytes = f_size(file) - info->data_offset;
      }
      data_found = 1U;
    }

    if ((fmt_found != 0U) && (data_found != 0U)) break;
    result = f_lseek(file, chunk_data + chunk_size + (chunk_size & 1U));
    if (result != FR_OK) return result;
  }

  if ((fmt_found == 0U) || (data_found == 0U) ||
      (info->block_align !=
       (uint16_t)(info->channels * (info->bits_per_sample / 8U))) ||
      (info->byte_rate != (info->sample_rate * info->block_align)) ||
      (Player_IsSupportedRate(info->sample_rate) == 0U))
  {
    return FR_INVALID_PARAMETER;
  }
  info->data_bytes -= info->data_bytes % info->block_align;
  return f_lseek(file, info->data_offset);
}

static uint32_t Player_FillSpdifUpsampledHalf(
    FIL *file, const PlayerWaveInfo *wave, uint8_t half,
    uint32_t loaded_bytes, uint8_t *io_error)
{
  uint32_t *output = (uint32_t *)&player_dma_buffer[half * PLAYER_HALF_BYTES];
  const uint32_t output_frames = PLAYER_HALF_BYTES / 8U;
  const uint32_t groups_needed = output_frames / PLAYER_SPDIF_FACTOR;
  const uint32_t remaining = wave->data_bytes - loaded_bytes;
  uint32_t source_frames = remaining / wave->block_align;
  uint32_t source_bytes;
  uint32_t output_group = 0U;
  UINT read = 0U;
  FRESULT result;

  player_last_fill_output_valid = 0U;
  if (source_frames > groups_needed) source_frames = groups_needed;
  source_bytes = source_frames * wave->block_align;
  if (source_bytes != 0U)
  {
    result = f_read(file, player_upsample_input, source_bytes, &read);
    if (result != FR_OK)
    {
      *io_error = (uint8_t)result;
      return 0U;
    }
    source_bytes = read - (read % wave->block_align);
    source_frames = source_bytes / wave->block_align;
  }

  for (uint32_t frame = 0U; frame < source_frames; ++frame)
  {
    const uint8_t *input = &player_upsample_input[frame * wave->block_align];
    const int16_t left = (int16_t)Player_ReadLe16(input);
    const int16_t right = (wave->channels == 2U) ?
        (int16_t)Player_ReadLe16(input + 2U) : left;
    int16_t interpolated[PLAYER_SPDIF_FACTOR * 2U];
    if (player_spdif_repeat != 0U)
    {
      for (uint32_t phase = 0U; phase < PLAYER_SPDIF_FACTOR; ++phase)
      {
        interpolated[phase * 2U] = left;
        interpolated[phase * 2U + 1U] = right;
      }
    }
    else if (player_spdif_iir != 0U)
    {
      SPDIF_IirUpsampler4x_Process(&player_spdif_iir_upsampler, left, right,
                                   interpolated);
    }
    else if (player_spdif_minphase_ns5 != 0U)
    {
      SPDIF_MinimumPhaseUpsampler4x_ProcessNoiseShaped5(
          &player_spdif_hybrid_upsampler, left, right, interpolated);
    }
    else if (player_spdif_minphase_ns2 != 0U)
    {
      SPDIF_MinimumPhaseUpsampler4x_ProcessNoiseShaped2(
          &player_spdif_hybrid_upsampler, left, right, interpolated);
    }
    else if (player_spdif_hybrid_ns2 != 0U)
    {
      SPDIF_HybridUpsampler4x_ProcessNoiseShaped2(
          &player_spdif_hybrid_upsampler, left, right, interpolated);
    }
    else if (player_spdif_hybrid != 0U)
    {
      SPDIF_HybridUpsampler4x_Process(&player_spdif_hybrid_upsampler,
                                      left, right, interpolated);
    }
    else
    {
      SPDIF_Upsampler4x_Process(&player_spdif_upsampler, left, right,
                                interpolated, player_spdif_headroom,
                                player_spdif_tpdf);
    }
    for (uint32_t phase = 0U; phase < PLAYER_SPDIF_FACTOR; ++phase)
    {
      output[(output_group * PLAYER_SPDIF_FACTOR + phase) * 2U] =
          SPDIF_TX_EncodeSample(interpolated[phase * 2U], 16U,
                                wave->sample_rate * PLAYER_SPDIF_FACTOR,
                                player_spdif_status_frame);
      output[(output_group * PLAYER_SPDIF_FACTOR + phase) * 2U + 1U] =
          SPDIF_TX_EncodeSample(interpolated[phase * 2U + 1U], 16U,
                                wave->sample_rate * PLAYER_SPDIF_FACTOR,
                                player_spdif_status_frame);
      player_spdif_status_frame =
          (player_spdif_status_frame + 1U) % 192U;
    }
    ++output_group;
  }

  if ((player_spdif_repeat == 0U) &&
      (source_bytes != 0U) &&
      ((loaded_bytes + source_bytes) >= wave->data_bytes))
  {
    if ((player_spdif_minphase_ns2 != 0U) ||
        (player_spdif_minphase_ns5 != 0U))
      player_spdif_tail_frames = SPDIF_IIR_UPSAMPLER_TAIL_FRAMES;
    else if ((player_spdif_hybrid != 0U) ||
        (player_spdif_hybrid_ns2 != 0U))
      player_spdif_tail_frames = SPDIF_HYBRID_UPSAMPLER_TAIL_FRAMES;
    else if (player_spdif_iir != 0U)
      player_spdif_tail_frames = SPDIF_IIR_UPSAMPLER_TAIL_FRAMES;
    else
      player_spdif_tail_frames = SPDIF_UPSAMPLER_DELAY_FRAMES;
  }

  /* Clock zero-valued source frames through the causal filter after EOF. FIR
   * mode emits its 16 delayed anchors; IIR and hybrid modes get a longer
   * finite drain so the recursive response falls below the 16-bit floor and
   * the phase-correction FIR history is cleared. */
  while ((output_group < groups_needed) && (player_spdif_tail_frames != 0U))
  {
    int16_t interpolated[PLAYER_SPDIF_FACTOR * 2U];
    if (player_spdif_iir != 0U)
    {
      SPDIF_IirUpsampler4x_Process(&player_spdif_iir_upsampler, 0, 0,
                                   interpolated);
    }
    else if (player_spdif_minphase_ns5 != 0U)
    {
      SPDIF_MinimumPhaseUpsampler4x_ProcessNoiseShaped5(
          &player_spdif_hybrid_upsampler, 0, 0, interpolated);
    }
    else if (player_spdif_minphase_ns2 != 0U)
    {
      SPDIF_MinimumPhaseUpsampler4x_ProcessNoiseShaped2(
          &player_spdif_hybrid_upsampler, 0, 0, interpolated);
    }
    else if (player_spdif_hybrid_ns2 != 0U)
    {
      SPDIF_HybridUpsampler4x_ProcessNoiseShaped2(
          &player_spdif_hybrid_upsampler, 0, 0, interpolated);
    }
    else if (player_spdif_hybrid != 0U)
    {
      SPDIF_HybridUpsampler4x_Process(&player_spdif_hybrid_upsampler, 0, 0,
                                      interpolated);
    }
    else
    {
      SPDIF_Upsampler4x_Process(&player_spdif_upsampler, 0, 0, interpolated,
                                player_spdif_headroom, player_spdif_tpdf);
    }
    for (uint32_t phase = 0U; phase < PLAYER_SPDIF_FACTOR; ++phase)
    {
      output[(output_group * PLAYER_SPDIF_FACTOR + phase) * 2U] =
          SPDIF_TX_EncodeSample(interpolated[phase * 2U], 16U,
                                wave->sample_rate * PLAYER_SPDIF_FACTOR,
                                player_spdif_status_frame);
      output[(output_group * PLAYER_SPDIF_FACTOR + phase) * 2U + 1U] =
          SPDIF_TX_EncodeSample(interpolated[phase * 2U + 1U], 16U,
                                wave->sample_rate * PLAYER_SPDIF_FACTOR,
                                player_spdif_status_frame);
      player_spdif_status_frame =
          (player_spdif_status_frame + 1U) % 192U;
    }
    ++output_group;
    --player_spdif_tail_frames;
  }

  player_last_fill_output_valid =
      ((source_frames != 0U) || (output_group != source_frames)) ? 1U : 0U;
  for (uint32_t frame = output_group * PLAYER_SPDIF_FACTOR;
       frame < output_frames; ++frame)
  {
    output[frame * 2U] =
        SPDIF_TX_EncodeSample(0, 16U,
                              wave->sample_rate * PLAYER_SPDIF_FACTOR,
                              player_spdif_status_frame);
    output[frame * 2U + 1U] =
        SPDIF_TX_EncodeSample(0, 16U,
                              wave->sample_rate * PLAYER_SPDIF_FACTOR,
                              player_spdif_status_frame);
    player_spdif_status_frame =
        (player_spdif_status_frame + 1U) % 192U;
  }
  __DMB();
  ++audio_player_refill_count;
  return source_bytes;
}

static uint32_t Player_FillHalf(FIL *file, const PlayerWaveInfo *wave,
                                uint8_t half, uint32_t loaded_bytes,
                                uint8_t *io_error)
{
  player_last_fill_output_valid = 0U;
  if ((player_output == USB_AUDIO_OUTPUT_SPDIF) &&
      (player_spdif_upsample != 0U))
  {
    return Player_FillSpdifUpsampledHalf(file, wave, half, loaded_bytes,
                                         io_error);
  }

  uint8_t *destination = &player_dma_buffer[half * PLAYER_HALF_BYTES];
  uint32_t remaining = wave->data_bytes - loaded_bytes;
  uint32_t output_frame_bytes =
      ((player_output == USB_AUDIO_OUTPUT_SPDIF) ||
       (wave->bits_per_sample == 24U)) ? 8U : 4U;
  uint32_t frame_capacity = PLAYER_HALF_BYTES / output_frame_bytes;
  uint32_t source_bytes;
  UINT read = 0U;
  FRESULT result;

  memset(destination, 0, PLAYER_HALF_BYTES);
  if (remaining == 0U) return 0U;

  source_bytes = remaining;
  if (source_bytes > (frame_capacity * wave->block_align))
  {
    source_bytes = frame_capacity * wave->block_align;
  }
  source_bytes -= source_bytes % wave->block_align;
  result = f_read(file, destination, source_bytes, &read);

  if (result != FR_OK)
  {
    *io_error = (uint8_t)result;
    return 0U;
  }
  if (read != source_bytes)
  {
    source_bytes = read - (read % wave->block_align);
  }
  if (source_bytes != 0U) player_last_fill_output_valid = 1U;

  /* S/PDIF always consumes one complete 32-bit DR word per sample, including
     24-bit MSB-aligned PCM and V/U/C. Expand backwards in place because the
     WAV source is packed more tightly than the SAI representation. */
  if (player_output == USB_AUDIO_OUTPUT_SPDIF)
  {
    uint32_t frames = source_bytes / wave->block_align;
    uint32_t *output = (uint32_t *)destination;
    const uint32_t first_status_frame = player_spdif_status_frame;
    for (uint32_t frame = frames; frame > 0U; --frame)
    {
      const uint8_t *input = &destination[(frame - 1U) * wave->block_align];
      int32_t left;
      int32_t right;
      uint32_t status_frame =
          (first_status_frame + frame - 1U) % 192U;
      if (wave->bits_per_sample == 24U)
      {
        left = Player_ReadLe24Signed(input);
        right = (wave->channels == 2U) ?
            Player_ReadLe24Signed(input + 3U) : left;
      }
      else
      {
        left = (int16_t)Player_ReadLe16(input);
        right = (wave->channels == 2U) ?
            (int16_t)Player_ReadLe16(input + 2U) : left;
      }
      output[(frame - 1U) * 2U] =
          SPDIF_TX_EncodeSample(left, wave->bits_per_sample,
                                wave->sample_rate, status_frame);
      output[(frame - 1U) * 2U + 1U] =
          SPDIF_TX_EncodeSample(right, wave->bits_per_sample,
                                wave->sample_rate, status_frame);
    }
    player_spdif_status_frame =
        (first_status_frame + frames) % 192U;
  }
  /* Expand backwards so packed 24-bit PCM can feed the WM8994 SAI1 path. */
  else if (wave->bits_per_sample == 24U)
  {
    uint32_t frames = source_bytes / wave->block_align;
    uint32_t *output = (uint32_t *)destination;
    for (uint32_t frame = frames; frame > 0U; --frame)
    {
      const uint8_t *input = &destination[(frame - 1U) * wave->block_align];
      int32_t left = Player_ReadLe24Signed(input);
      int32_t right = (wave->channels == 2U) ?
          Player_ReadLe24Signed(input + 3U) : left;
      /* SAI1 consumes the low 24 PCM bits. Keeping bits 31:24 clear lets the
         same word buffer feed SAI2 S/PDIF without asserting V/U/C flags. */
      output[(frame - 1U) * 2U] = (uint32_t)left & 0x00FFFFFFUL;
      output[(frame - 1U) * 2U + 1U] = (uint32_t)right & 0x00FFFFFFUL;
    }
  }
  else if (wave->channels == 1U)
  {
    uint32_t frames = source_bytes / wave->block_align;
    int16_t *output = (int16_t *)destination;
    for (uint32_t frame = frames; frame > 0U; --frame)
    {
      int16_t sample =
          (int16_t)Player_ReadLe16(&destination[(frame - 1U) * 2U]);
      output[(frame - 1U) * 2U] = sample;
      output[(frame - 1U) * 2U + 1U] = sample;
    }
  }
  __DMB();
  ++audio_player_refill_count;
  return source_bytes;
}

static uint8_t Player_CodecInit(uint32_t sample_rate, uint16_t bits_per_sample,
                                 uint8_t volume)
{
  uint8_t result;
  player_output = USB_Audio_GetOutput();
  player_spdif_upsample = 0U;
  player_spdif_headroom = 0U;
  player_spdif_repeat = 0U;
  player_spdif_tpdf = 0U;
  player_spdif_iir = 0U;
  player_spdif_hybrid = 0U;
  player_spdif_hybrid_ns2 = 0U;
  player_spdif_minphase_ns2 = 0U;
  player_spdif_minphase_ns5 = 0U;
  player_spdif_tail_frames = 0U;
  SPDIF_Upsampler4x_Reset(&player_spdif_upsampler);
  SPDIF_IirUpsampler4x_Init(&player_spdif_iir_upsampler, sample_rate);
  SPDIF_HybridUpsampler4x_Init(&player_spdif_hybrid_upsampler, sample_rate);
  if (player_output == USB_AUDIO_OUTPUT_SPDIF)
  {
    const USB_AudioSpdifMode spdif_mode = USB_Audio_GetSpdifMode();
    if (spdif_mode == USB_AUDIO_SPDIF_UPSAMPLE_4X_BESSEL_MINPHASE_NS5)
    {
      SPDIF_MinimumPhaseUpsampler4x_InitBesselNoiseShaped5(
          &player_spdif_hybrid_upsampler, sample_rate);
    }
    else if (spdif_mode == USB_AUDIO_SPDIF_UPSAMPLE_4X_BESSEL_OPEN_NS2)
    {
      SPDIF_MinimumPhaseUpsampler4x_InitBesselOpen(
          &player_spdif_hybrid_upsampler, sample_rate);
    }
    else if (spdif_mode == USB_AUDIO_SPDIF_UPSAMPLE_4X_BESSEL_MINPHASE_NS2)
    {
      SPDIF_MinimumPhaseUpsampler4x_InitBessel(
          &player_spdif_hybrid_upsampler, sample_rate);
    }
    else if ((spdif_mode == USB_AUDIO_SPDIF_UPSAMPLE_4X_BUTTERWORTH_NS2) ||
        (spdif_mode ==
         USB_AUDIO_SPDIF_UPSAMPLE_4X_BUTTERWORTH_MINPHASE_NS2))
    {
      SPDIF_HybridUpsampler4x_InitButterworth(
          &player_spdif_hybrid_upsampler, sample_rate);
    }
    player_spdif_upsample =
        ((spdif_mode != USB_AUDIO_SPDIF_NATIVE) &&
         (bits_per_sample == 16U) &&
         ((sample_rate == 44100U) || (sample_rate == 48000U))) ? 1U : 0U;
    player_spdif_headroom =
        ((player_spdif_upsample != 0U) &&
         ((spdif_mode == USB_AUDIO_SPDIF_UPSAMPLE_4X_HEADROOM) ||
          (spdif_mode == USB_AUDIO_SPDIF_UPSAMPLE_4X_TPDF))) ? 1U : 0U;
    player_spdif_repeat =
        ((player_spdif_upsample != 0U) &&
         (spdif_mode == USB_AUDIO_SPDIF_REPEAT_4X)) ? 1U : 0U;
    player_spdif_tpdf =
        ((player_spdif_upsample != 0U) &&
         (spdif_mode == USB_AUDIO_SPDIF_UPSAMPLE_4X_TPDF)) ? 1U : 0U;
    player_spdif_iir =
        ((player_spdif_upsample != 0U) &&
         (spdif_mode == USB_AUDIO_SPDIF_UPSAMPLE_4X_IIR)) ? 1U : 0U;
    player_spdif_hybrid =
        ((player_spdif_upsample != 0U) &&
         (spdif_mode == USB_AUDIO_SPDIF_UPSAMPLE_4X_HYBRID)) ? 1U : 0U;
    player_spdif_hybrid_ns2 =
        ((player_spdif_upsample != 0U) &&
         ((spdif_mode == USB_AUDIO_SPDIF_UPSAMPLE_4X_HYBRID_NS2) ||
          (spdif_mode == USB_AUDIO_SPDIF_UPSAMPLE_4X_BUTTERWORTH_NS2))) ?
        1U : 0U;
    player_spdif_minphase_ns2 =
        ((player_spdif_upsample != 0U) &&
         ((spdif_mode ==
           USB_AUDIO_SPDIF_UPSAMPLE_4X_BUTTERWORTH_MINPHASE_NS2) ||
          (spdif_mode ==
           USB_AUDIO_SPDIF_UPSAMPLE_4X_BESSEL_MINPHASE_NS2) ||
          (spdif_mode ==
           USB_AUDIO_SPDIF_UPSAMPLE_4X_BESSEL_OPEN_NS2))) ? 1U : 0U;
    player_spdif_minphase_ns5 =
        ((player_spdif_upsample != 0U) &&
         (spdif_mode ==
          USB_AUDIO_SPDIF_UPSAMPLE_4X_BESSEL_MINPHASE_NS5)) ? 1U : 0U;
    return (SPDIF_TX_Init(player_spdif_upsample != 0U ?
                          sample_rate * PLAYER_SPDIF_FACTOR : sample_rate,
                          bits_per_sample)
            != 0U) ?
           AUDIO_OK : AUDIO_ERROR;
  }

  if (Touch_I2C_Reserve(500U) == 0U) return AUDIO_ERROR;
  result = BSP_AUDIO_OUT_InitEx(OUTPUT_DEVICE_HEADPHONE, volume, sample_rate,
                                bits_per_sample);
  if (result == AUDIO_OK)
  {
    BSP_AUDIO_OUT_SetAudioFrameSlot(CODEC_AUDIOFRAME_SLOT_02);
  }
  Touch_I2C_Release();
  return result;
}

static uint8_t Player_CodecCommand(uint8_t command, uint8_t value)
{
  uint8_t result = AUDIO_ERROR;
  if (player_output == USB_AUDIO_OUTPUT_SPDIF)
  {
    if (command == PLAYER_COMMAND_TOGGLE)
    {
      if (value != 0U) SPDIF_TX_Pause();
      else SPDIF_TX_Resume();
      result = AUDIO_OK;
    }
    else if (command == PLAYER_COMMAND_VOLUME)
    {
      /* Keep S/PDIF bit-perfect; volume is controlled by the receiver. */
      result = AUDIO_OK;
    }
    else if (command == PLAYER_COMMAND_STOP)
    {
      SPDIF_TX_Stop();
      result = AUDIO_OK;
    }
    return result;
  }

  if (Touch_I2C_Reserve(500U) == 0U) return AUDIO_ERROR;
  if (command == PLAYER_COMMAND_TOGGLE)
  {
    if (value != 0U)
    {
      result = BSP_AUDIO_OUT_Pause();
    }
    else
    {
      result = BSP_AUDIO_OUT_Resume();
    }
  }
  else if (command == PLAYER_COMMAND_VOLUME)
  {
    result = BSP_AUDIO_OUT_SetVolume(value);
  }
  else if (command == PLAYER_COMMAND_STOP)
  {
    result = BSP_AUDIO_OUT_Stop(CODEC_PDWN_SW);
  }
  Touch_I2C_Release();
  return result;
}

static uint8_t Player_StartOutput(void)
{
  uint8_t result;
  if (player_output == USB_AUDIO_OUTPUT_SPDIF)
  {
    return (SPDIF_TX_Start(player_dma_buffer, PLAYER_DMA_BYTES) != 0U) ?
           AUDIO_OK : AUDIO_ERROR;
  }

  if (Touch_I2C_Reserve(500U) == 0U) return AUDIO_ERROR;
  result = BSP_AUDIO_OUT_Play((uint16_t *)player_dma_buffer, PLAYER_DMA_BYTES);
  Touch_I2C_Release();
  return result;
}

static void Player_Close(FIL *file, uint8_t *file_open, uint8_t stop_hardware)
{
  player_active = 0U;
  if (stop_hardware != 0U)
  {
    (void)Player_CodecCommand(PLAYER_COMMAND_STOP, 0U);
    if (player_output == USB_AUDIO_OUTPUT_SPDIF) SPDIF_TX_DeInit();
    else BSP_AUDIO_OUT_DeInit();
  }
  if (*file_open != 0U)
  {
    (void)f_close(file);
    *file_open = 0U;
  }
}

void AudioPlayer_Task(void const *argument)
{
  FIL file;
  PlayerWaveInfo wave;
  PlayerCommand command;
  AudioPlayerSnapshot snapshot;
  uint32_t notify_bits;
  uint32_t loaded_bytes = 0U;
  uint32_t played_bytes = 0U;
  uint32_t valid_bytes[2] = {0U, 0U};
  uint8_t valid_output[2] = {0U, 0U};
  uint8_t file_open = 0U;
  uint8_t hardware_ready = 0U;
  uint8_t paused = 0U;
  uint8_t io_error = 0U;
  char path[24];
  FRESULT result;

  (void)argument;
  player_task_handle = xTaskGetCurrentTaskHandle();
  AudioPlayer_GetSnapshot(&snapshot);

  for (;;)
  {
    notify_bits = 0U;
    (void)xTaskNotifyWait(0U, 0xFFFFFFFFUL, &notify_bits, pdMS_TO_TICKS(50U));

    if (audio_player_debug_command != 0U)
    {
      memset(&command, 0, sizeof(command));
      if (audio_player_debug_command == 1U)
      {
        command.type = PLAYER_COMMAND_PLAY;
        strncpy(command.filename, "1.wav", sizeof(command.filename) - 1U);
      }
      else if (audio_player_debug_command == 2U)
      {
        command.type = PLAYER_COMMAND_TOGGLE;
      }
      else
      {
        command.type = PLAYER_COMMAND_STOP;
      }
      audio_player_debug_command = 0U;
      (void)xQueueSendToFront(player_command_queue, &command, 0U);
    }

    while (xQueueReceive(player_command_queue, &command, 0U) == pdPASS)
    {
      if (command.type == PLAYER_COMMAND_PLAY)
      {
        Player_Close(&file, &file_open, hardware_ready);
        hardware_ready = 0U;
        paused = 0U;
        memset(&wave, 0, sizeof(wave));
        memset(&snapshot, 0, sizeof(snapshot));
        snapshot.status = AUDIO_PLAYER_LOADING;
        snapshot.volume = player_volume;
        strncpy(snapshot.filename, command.filename, sizeof(snapshot.filename) - 1U);
        Player_Publish(&snapshot);

        if ((SD_Card_IsPresent() == 0U) || (sd_storage_status != SD_STORAGE_READY))
        {
          snapshot.status = AUDIO_PLAYER_NO_CARD;
          Player_Publish(&snapshot);
          continue;
        }
        if ((AudioRecorder_IsBusy() != 0U) ||
            (USB_Audio_ClaimsCodec() != 0U))
        {
          snapshot.status = AUDIO_PLAYER_BUSY;
          Player_Publish(&snapshot);
          continue;
        }
        if (AudioSpectrum_IsBusy() != 0U)
        {
          snapshot.status = AUDIO_PLAYER_BUSY;
          Player_Publish(&snapshot);
          continue;
        }

        player_active = 1U;
        (void)snprintf(path, sizeof(path), "%s/%s", USERPath, snapshot.filename);
        result = f_open(&file, path, FA_READ);
        if (result == FR_OK)
        {
          file_open = 1U;
          result = Player_ParseWave(&file, &wave);
        }
        if (result != FR_OK)
        {
          snapshot.status = (result == FR_INVALID_PARAMETER ||
                             result == FR_INVALID_OBJECT) ?
                            AUDIO_PLAYER_UNSUPPORTED : AUDIO_PLAYER_ERROR;
          snapshot.last_fatfs_result = (uint8_t)result;
          ++audio_player_error_count;
          Player_Close(&file, &file_open, 0U);
          Player_Publish(&snapshot);
          continue;
        }

        snapshot.channels = (uint8_t)wave.channels;
        snapshot.bits_per_sample = (uint8_t)wave.bits_per_sample;
        snapshot.sample_rate = wave.sample_rate;
        snapshot.data_bytes = wave.data_bytes;
        snapshot.duration_ms = wave.byte_rate == 0U ? 0U :
            (uint32_t)(((uint64_t)wave.data_bytes * 1000U) / wave.byte_rate);
        if (Player_CodecInit(wave.sample_rate, wave.bits_per_sample,
                             snapshot.volume) != AUDIO_OK)
        {
          snapshot.status = AUDIO_PLAYER_ERROR;
          ++audio_player_error_count;
          if (player_output == USB_AUDIO_OUTPUT_SPDIF) SPDIF_TX_DeInit();
          else BSP_AUDIO_OUT_DeInit();
          Player_Close(&file, &file_open, 0U);
          Player_Publish(&snapshot);
          continue;
        }
        hardware_ready = 1U;
        player_spdif_status_frame = 0U;
        loaded_bytes = 0U;
        played_bytes = 0U;
        io_error = 0U;
        valid_bytes[0] = Player_FillHalf(&file, &wave, 0U, loaded_bytes, &io_error);
        valid_output[0] = player_last_fill_output_valid;
        loaded_bytes += valid_bytes[0];
        valid_bytes[1] = Player_FillHalf(&file, &wave, 1U, loaded_bytes, &io_error);
        valid_output[1] = player_last_fill_output_valid;
        loaded_bytes += valid_bytes[1];
        if ((io_error != 0U) || (valid_output[0] == 0U))
        {
          snapshot.status = AUDIO_PLAYER_ERROR;
          snapshot.last_fatfs_result = io_error;
          ++audio_player_error_count;
          Player_Close(&file, &file_open, hardware_ready);
          hardware_ready = 0U;
          Player_Publish(&snapshot);
          continue;
        }
        result = (FRESULT)Player_StartOutput();
        if ((uint8_t)result != AUDIO_OK)
        {
          snapshot.status = AUDIO_PLAYER_ERROR;
          ++audio_player_error_count;
          Player_Close(&file, &file_open, hardware_ready);
          hardware_ready = 0U;
          Player_Publish(&snapshot);
          continue;
        }
        player_active = 1U;
        notify_bits &= ~(PLAYER_NOTIFY_HALF | PLAYER_NOTIFY_FULL);
        snapshot.status = AUDIO_PLAYER_PLAYING;
        Player_UpdatePosition(&snapshot, 0U);
        Player_Publish(&snapshot);
      }
      else if (command.type == PLAYER_COMMAND_TOGGLE)
      {
        if (snapshot.status == AUDIO_PLAYER_PLAYING)
        {
          if (Player_CodecCommand(PLAYER_COMMAND_TOGGLE, 1U) == AUDIO_OK)
          {
            paused = 1U;
            snapshot.status = AUDIO_PLAYER_PAUSED;
            Player_Publish(&snapshot);
          }
        }
        else if (snapshot.status == AUDIO_PLAYER_PAUSED)
        {
          if (Player_CodecCommand(PLAYER_COMMAND_TOGGLE, 0U) == AUDIO_OK)
          {
            paused = 0U;
            snapshot.status = AUDIO_PLAYER_PLAYING;
            Player_Publish(&snapshot);
          }
        }
      }
      else if (command.type == PLAYER_COMMAND_VOLUME)
      {
        snapshot.volume = (uint8_t)command.value;
        player_volume = snapshot.volume;
        if ((hardware_ready == 0U) ||
            (Player_CodecCommand(PLAYER_COMMAND_VOLUME, snapshot.volume) == AUDIO_OK))
        {
          Player_Publish(&snapshot);
        }
      }
      else if (command.type == PLAYER_COMMAND_SEEK)
      {
        if ((file_open != 0U) && (hardware_ready != 0U) &&
            ((snapshot.status == AUDIO_PLAYER_PLAYING) ||
             (snapshot.status == AUDIO_PLAYER_PAUSED)))
        {
          uint32_t target = (uint32_t)(((uint64_t)wave.data_bytes * command.value) / 1000U);
          target -= target % wave.block_align;
          (void)Player_CodecCommand(PLAYER_COMMAND_STOP, 0U);
          result = f_lseek(&file, wave.data_offset + target);
          if (result == FR_OK)
          {
            player_spdif_status_frame = 0U;
            player_spdif_tail_frames = 0U;
            SPDIF_Upsampler4x_Reset(&player_spdif_upsampler);
            SPDIF_IirUpsampler4x_Reset(&player_spdif_iir_upsampler);
            SPDIF_HybridUpsampler4x_Reset(&player_spdif_hybrid_upsampler);
            loaded_bytes = target;
            played_bytes = target;
            io_error = 0U;
            valid_bytes[0] = Player_FillHalf(&file, &wave, 0U, loaded_bytes, &io_error);
            valid_output[0] = player_last_fill_output_valid;
            loaded_bytes += valid_bytes[0];
            valid_bytes[1] = Player_FillHalf(&file, &wave, 1U, loaded_bytes, &io_error);
            valid_output[1] = player_last_fill_output_valid;
            loaded_bytes += valid_bytes[1];
            if ((io_error == 0U) && (valid_output[0] != 0U) &&
                (Player_StartOutput() == AUDIO_OK))
            {
              if (paused != 0U)
              {
                (void)Player_CodecCommand(PLAYER_COMMAND_TOGGLE, 1U);
              }
              notify_bits &= ~(PLAYER_NOTIFY_HALF | PLAYER_NOTIFY_FULL);
              Player_UpdatePosition(&snapshot, played_bytes);
            }
            else if ((io_error == 0U) && (valid_output[0] == 0U))
            {
              Player_Close(&file, &file_open, hardware_ready);
              hardware_ready = 0U;
              snapshot.status = AUDIO_PLAYER_FINISHED;
              Player_UpdatePosition(&snapshot, snapshot.data_bytes);
              Player_Publish(&snapshot);
            }
            else
            {
              result = FR_DISK_ERR;
            }
          }
          if ((result != FR_OK) && (snapshot.status != AUDIO_PLAYER_FINISHED))
          {
            snapshot.status = AUDIO_PLAYER_ERROR;
            snapshot.last_fatfs_result = (uint8_t)result;
            ++audio_player_error_count;
            Player_Close(&file, &file_open, hardware_ready);
            hardware_ready = 0U;
            Player_Publish(&snapshot);
          }
        }
      }
      else if (command.type == PLAYER_COMMAND_STOP)
      {
        Player_Close(&file, &file_open, hardware_ready);
        hardware_ready = 0U;
        paused = 0U;
        snapshot.status = AUDIO_PLAYER_IDLE;
        snapshot.position_bytes = 0U;
        snapshot.position_ms = 0U;
        Player_Publish(&snapshot);
      }
    }

    if ((player_active != 0U) && (SD_Card_IsPresent() == 0U))
    {
      Player_Close(&file, &file_open, hardware_ready);
      hardware_ready = 0U;
      snapshot.status = AUDIO_PLAYER_NO_CARD;
      snapshot.last_fatfs_result = (uint8_t)FR_NOT_READY;
      Player_Publish(&snapshot);
      continue;
    }

    if (player_active != 0U)
    {
      for (uint8_t half = 0U; half < 2U; ++half)
      {
        const uint32_t bit = (half == 0U) ? PLAYER_NOTIFY_HALF : PLAYER_NOTIFY_FULL;
        if ((notify_bits & bit) == 0U) continue;
        played_bytes += valid_bytes[half];
        valid_bytes[half] = Player_FillHalf(&file, &wave, half, loaded_bytes, &io_error);
        valid_output[half] = player_last_fill_output_valid;
        loaded_bytes += valid_bytes[half];
        Player_UpdatePosition(&snapshot, played_bytes);
        if (io_error != 0U)
        {
          snapshot.status = AUDIO_PLAYER_ERROR;
          snapshot.last_fatfs_result = io_error;
          ++audio_player_error_count;
          Player_Close(&file, &file_open, hardware_ready);
          hardware_ready = 0U;
          Player_Publish(&snapshot);
          break;
        }
        if ((valid_output[0] == 0U) && (valid_output[1] == 0U))
        {
          Player_Close(&file, &file_open, hardware_ready);
          hardware_ready = 0U;
          snapshot.status = AUDIO_PLAYER_FINISHED;
          Player_UpdatePosition(&snapshot, snapshot.data_bytes);
          Player_Publish(&snapshot);
          break;
        }
      }
    }
  }
}

void BSP_AUDIO_OUT_HalfTransfer_CallBack(void)
{
  if (USB_Audio_DmaIsRunning() != 0U)
  {
    USB_Audio_DmaHalfCallback();
    return;
  }
  BaseType_t higher_priority_task_woken = pdFALSE;
  ++audio_player_dma_half_count;
  if (player_task_handle != NULL)
  {
    (void)xTaskNotifyFromISR(player_task_handle, PLAYER_NOTIFY_HALF,
                             eSetBits, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
  }
}

void BSP_AUDIO_OUT_TransferComplete_CallBack(void)
{
  if (USB_Audio_DmaIsRunning() != 0U)
  {
    USB_Audio_DmaFullCallback();
    return;
  }
  BaseType_t higher_priority_task_woken = pdFALSE;
  ++audio_player_dma_full_count;
  if (player_task_handle != NULL)
  {
    (void)xTaskNotifyFromISR(player_task_handle, PLAYER_NOTIFY_FULL,
                             eSetBits, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
  }
}

void BSP_AUDIO_OUT_Error_CallBack(void)
{
  if (USB_Audio_IsActive() != 0U)
  {
    USB_Audio_DmaErrorCallback();
    return;
  }
  ++audio_player_error_count;
}
