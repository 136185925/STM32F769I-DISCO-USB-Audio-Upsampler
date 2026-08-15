#include "audio_diag_log.h"

#include "stm32f7xx_hal.h"

#include <stdio.h>
#include <string.h>

#define AUDIO_DIAG_RING_RECORDS       64U
#define AUDIO_DIAG_BATCH_RECORDS      16U
#define AUDIO_DIAG_FLUSH_DELAY_MS     5000U
#define AUDIO_DIAG_LOG_PATH           "0:/USBXRUN.CSV"

typedef struct
{
  uint32_t sequence;
  uint32_t tick_ms;
  uint32_t sample_rate;
  uint32_t queued_frames;
  uint32_t underruns;
  uint32_t overruns;
  uint32_t feedback_q16;
  uint32_t feedback_tx;
  uint32_t feedback_errors;
  uint32_t arg0;
  uint32_t arg1;
  uint32_t dropped;
  uint8_t event;
  uint8_t output;
  uint8_t dma_running;
  uint8_t high_speed;
} AudioDiagRecord;

static AudioDiagRecord audio_diag_ring[AUDIO_DIAG_RING_RECORDS];
static AudioDiagRecord audio_diag_batch[AUDIO_DIAG_BATCH_RECORDS];
static char audio_diag_line[256];
static FIL audio_diag_file;
static volatile uint16_t audio_diag_head;
static volatile uint16_t audio_diag_tail;
static volatile uint16_t audio_diag_count;
static volatile uint32_t audio_diag_sequence;
static volatile uint32_t audio_diag_first_pending_tick;

volatile uint32_t audio_diag_dropped_count;
volatile uint32_t audio_diag_write_error_count;
volatile uint32_t audio_diag_written_record_count;

static uint32_t AudioDiag_EnterCritical(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  __DMB();
  return primask;
}

static void AudioDiag_ExitCritical(uint32_t primask)
{
  __DMB();
  if (primask == 0U)
  {
    __enable_irq();
  }
}

static const char *AudioDiag_EventName(uint8_t event)
{
  switch ((AudioDiagEvent)event)
  {
    case AUDIO_DIAG_BOOT:             return "BOOT";
    case AUDIO_DIAG_ENABLE_REQUEST:   return "ENABLE_REQUEST";
    case AUDIO_DIAG_DEVICE_STATE:     return "DEVICE_STATE";
    case AUDIO_DIAG_SESSION_PREPARE:  return "SESSION_PREPARE";
    case AUDIO_DIAG_SESSION_RELEASE:  return "SESSION_RELEASE";
    case AUDIO_DIAG_RECONFIG_BEGIN:   return "RECONFIG_BEGIN";
    case AUDIO_DIAG_RECONFIG_OK:      return "RECONFIG_OK";
    case AUDIO_DIAG_RECONFIG_FAIL:    return "RECONFIG_FAIL";
    case AUDIO_DIAG_RATE_REQUEST:     return "RATE_REQUEST";
    case AUDIO_DIAG_OUTPUT_REQUEST:   return "OUTPUT_REQUEST";
    case AUDIO_DIAG_DMA_START:        return "DMA_START";
    case AUDIO_DIAG_DMA_START_FAIL:   return "DMA_START_FAIL";
    case AUDIO_DIAG_DMA_UNDERRUN:     return "DMA_UNDERRUN";
    case AUDIO_DIAG_REBUFFER_BEGIN:   return "REBUFFER_BEGIN";
    case AUDIO_DIAG_DMA_ERROR:        return "DMA_ERROR";
    case AUDIO_DIAG_RING_OVERRUN:     return "RING_OVERRUN";
    case AUDIO_DIAG_FEEDBACK_ERROR:   return "FEEDBACK_ERROR";
    case AUDIO_DIAG_STATUS:           return "STATUS";
    default:                          return "UNKNOWN";
  }
}

static const char *AudioDiag_OutputName(uint8_t output)
{
  return (output == 1U) ? "SPDIF" : "WM8994";
}

void AudioDiag_Log(AudioDiagEvent event, uint32_t sample_rate,
                   uint8_t output, uint8_t dma_running, uint8_t high_speed,
                   uint32_t queued_frames, uint32_t underruns,
                   uint32_t overruns, uint32_t feedback_q16,
                   uint32_t feedback_tx, uint32_t feedback_errors,
                   uint32_t arg0, uint32_t arg1)
{
  AudioDiagRecord *record;
  uint32_t primask;
  uint32_t now = HAL_GetTick();

  primask = AudioDiag_EnterCritical();
  if (audio_diag_count >= AUDIO_DIAG_RING_RECORDS)
  {
    ++audio_diag_dropped_count;
    AudioDiag_ExitCritical(primask);
    return;
  }

  record = &audio_diag_ring[audio_diag_head];
  record->sequence = ++audio_diag_sequence;
  record->tick_ms = now;
  record->sample_rate = sample_rate;
  record->queued_frames = queued_frames;
  record->underruns = underruns;
  record->overruns = overruns;
  record->feedback_q16 = feedback_q16;
  record->feedback_tx = feedback_tx;
  record->feedback_errors = feedback_errors;
  record->arg0 = arg0;
  record->arg1 = arg1;
  record->dropped = audio_diag_dropped_count;
  record->event = (uint8_t)event;
  record->output = output;
  record->dma_running = dma_running;
  record->high_speed = high_speed;

  audio_diag_head = (uint16_t)((audio_diag_head + 1U) %
                               AUDIO_DIAG_RING_RECORDS);
  if (audio_diag_count == 0U)
  {
    audio_diag_first_pending_tick = now;
  }
  ++audio_diag_count;
  AudioDiag_ExitCritical(primask);
}

static FRESULT AudioDiag_Write(const void *data, UINT length)
{
  UINT written = 0U;
  FRESULT result = f_write(&audio_diag_file, data, length, &written);
  if ((result == FR_OK) && (written != length))
  {
    result = FR_DISK_ERR;
  }
  return result;
}

FRESULT AudioDiag_Service(void)
{
  static const char header[] =
      "seq,ms,event,rate_hz,output,dma,hs,queued_frames,underruns,overruns,"
      "feedback_q16,feedback_ppm,feedback_tx,feedback_errors,arg0,arg1,dropped\r\n";
  uint32_t primask;
  uint32_t pending_since;
  uint32_t now;
  uint16_t batch_count;
  uint16_t tail;
  uint16_t index;
  FRESULT result;

  now = HAL_GetTick();
  primask = AudioDiag_EnterCritical();
  batch_count = audio_diag_count;
  pending_since = audio_diag_first_pending_tick;
  if (batch_count > AUDIO_DIAG_BATCH_RECORDS)
  {
    batch_count = AUDIO_DIAG_BATCH_RECORDS;
  }
  if ((batch_count == 0U) ||
      ((audio_diag_count < AUDIO_DIAG_BATCH_RECORDS) &&
       ((now - pending_since) < AUDIO_DIAG_FLUSH_DELAY_MS)))
  {
    AudioDiag_ExitCritical(primask);
    return FR_OK;
  }

  tail = audio_diag_tail;
  for (index = 0U; index < batch_count; ++index)
  {
    audio_diag_batch[index] = audio_diag_ring[tail];
    tail = (uint16_t)((tail + 1U) % AUDIO_DIAG_RING_RECORDS);
  }
  AudioDiag_ExitCritical(primask);

  result = f_open(&audio_diag_file, AUDIO_DIAG_LOG_PATH,
                  FA_OPEN_ALWAYS | FA_WRITE);
  if (result != FR_OK)
  {
    ++audio_diag_write_error_count;
    return result;
  }

  if (f_size(&audio_diag_file) == 0U)
  {
    result = AudioDiag_Write(header, (UINT)(sizeof(header) - 1U));
  }
  else
  {
    result = f_lseek(&audio_diag_file, f_size(&audio_diag_file));
  }

  for (index = 0U; (index < batch_count) && (result == FR_OK); ++index)
  {
    const AudioDiagRecord *record = &audio_diag_batch[index];
    int32_t feedback_ppm = 0;
    int length;

    if (record->sample_rate != 0U)
    {
      const int64_t nominal_q16 = (int64_t)record->sample_rate << 16U;
      feedback_ppm = (int32_t)((((int64_t)record->feedback_q16 -
                                nominal_q16) * 1000000LL) /
                              nominal_q16);
    }

    length = snprintf(audio_diag_line, sizeof(audio_diag_line),
        "%lu,%lu,%s,%lu,%s,%u,%u,%lu,%lu,%lu,%lu,%ld,%lu,%lu,%lu,%lu,%lu\r\n",
        (unsigned long)record->sequence,
        (unsigned long)record->tick_ms,
        AudioDiag_EventName(record->event),
        (unsigned long)record->sample_rate,
        AudioDiag_OutputName(record->output),
        (unsigned int)record->dma_running,
        (unsigned int)record->high_speed,
        (unsigned long)record->queued_frames,
        (unsigned long)record->underruns,
        (unsigned long)record->overruns,
        (unsigned long)record->feedback_q16,
        (long)feedback_ppm,
        (unsigned long)record->feedback_tx,
        (unsigned long)record->feedback_errors,
        (unsigned long)record->arg0,
        (unsigned long)record->arg1,
        (unsigned long)record->dropped);
    if ((length <= 0) || ((size_t)length >= sizeof(audio_diag_line)))
    {
      result = FR_INT_ERR;
    }
    else
    {
      result = AudioDiag_Write(audio_diag_line, (UINT)length);
    }
  }

  if (result == FR_OK)
  {
    result = f_sync(&audio_diag_file);
  }
  {
    FRESULT close_result = f_close(&audio_diag_file);
    if ((result == FR_OK) && (close_result != FR_OK))
    {
      result = close_result;
    }
  }

  if (result != FR_OK)
  {
    ++audio_diag_write_error_count;
    return result;
  }

  primask = AudioDiag_EnterCritical();
  audio_diag_tail = (uint16_t)((audio_diag_tail + batch_count) %
                               AUDIO_DIAG_RING_RECORDS);
  audio_diag_count = (uint16_t)(audio_diag_count - batch_count);
  audio_diag_written_record_count += batch_count;
  if (audio_diag_count == 0U)
  {
    audio_diag_first_pending_tick = 0U;
  }
  AudioDiag_ExitCritical(primask);
  return FR_OK;
}
