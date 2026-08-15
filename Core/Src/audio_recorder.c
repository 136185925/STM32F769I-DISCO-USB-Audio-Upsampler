#include "audio_recorder.h"

#include "audio_codec.h"
#include "audio_player.h"
#include "audio_spectrum.h"
#include "usb_audio.h"
#include "fatfs.h"
#include "sdmmc.h"
#include "stm32f769i_discovery_audio.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

#define RECORDER_SAMPLE_RATE        16000U
#define RECORDER_MIC_COUNT          4U
#define RECORDER_PCM_HALFWORDS      8192U
#define RECORDER_HALF_HALFWORDS     (RECORDER_PCM_HALFWORDS / 2U)
#define RECORDER_MONO_SAMPLES       (RECORDER_HALF_HALFWORDS / RECORDER_MIC_COUNT)
#define RECORDER_RING_CHUNKS        8U
#define RECORDER_SCRATCH_WORDS      512U
#define RECORDER_QUEUE_LENGTH       12U
#define RECORDER_SYNC_INTERVAL      (RECORDER_SAMPLE_RATE * 2U * 5U)

typedef enum
{
  RECORDER_EVENT_START = 1,
  RECORDER_EVENT_STOP,
  RECORDER_EVENT_AUDIO_CHUNK
} RecorderEventType;

typedef struct
{
  uint8_t type;
  uint8_t index;
} RecorderEvent;

volatile uint32_t audio_recorder_dma_half_count = 0U;
volatile uint32_t audio_recorder_dma_full_count = 0U;
volatile uint32_t audio_recorder_write_count = 0U;
volatile uint32_t audio_recorder_error_count = 0U;
volatile uint8_t audio_recorder_debug_command = 0U;

/* DFSDM DMA cannot access DTCM. This section is linked into the dedicated
   non-cacheable upper-SRAM DMA region configured in main.c. */
__attribute__((section(".dma_buffer"), aligned(32)))
static int32_t recorder_scratch[RECORDER_SCRATCH_WORDS];

/* The BSP assembles four interleaved 16-bit microphone channels here in ISR
   context. This buffer is CPU-only and can remain in fast DTCM. */
static uint16_t recorder_pcm[RECORDER_PCM_HALFWORDS];
static int16_t recorder_ring[RECORDER_RING_CHUNKS][RECORDER_MONO_SAMPLES];

static QueueHandle_t recorder_queue;
static AudioRecorderSnapshot recorder_snapshot;
static volatile uint8_t recorder_capture_active;
static volatile uint8_t recorder_ring_write;

static void Recorder_Publish(AudioRecorderSnapshot *snapshot)
{
  taskENTER_CRITICAL();
  snapshot->generation = recorder_snapshot.generation + 1U;
  recorder_snapshot = *snapshot;
  taskEXIT_CRITICAL();
}

void AudioRecorder_GetSnapshot(AudioRecorderSnapshot *snapshot)
{
  if (snapshot == NULL)
  {
    return;
  }
  taskENTER_CRITICAL();
  *snapshot = recorder_snapshot;
  taskEXIT_CRITICAL();
}

uint8_t AudioRecorder_IsBusy(void)
{
  AudioRecorderStatus status = recorder_snapshot.status;
  return ((status == AUDIO_RECORDER_STARTING) ||
          (status == AUDIO_RECORDER_RECORDING) ||
          (status == AUDIO_RECORDER_STOPPING)) ? 1U : 0U;
}

void AudioRecorder_CreateResources(void)
{
  memset(&recorder_snapshot, 0, sizeof(recorder_snapshot));
  recorder_snapshot.status = AUDIO_RECORDER_READY;
  recorder_snapshot.codec_id = audio_codec_id;
  recorder_snapshot.codec_ready = audio_codec_ready;
  recorder_queue = xQueueCreate(RECORDER_QUEUE_LENGTH, sizeof(RecorderEvent));
}

static void Recorder_SendCommand(uint8_t type)
{
  RecorderEvent event;
  event.type = type;
  event.index = 0U;
  if ((recorder_queue != NULL) &&
      (xQueueSend(recorder_queue, &event, pdMS_TO_TICKS(20U)) != pdPASS))
  {
    ++audio_recorder_error_count;
  }
}

void AudioRecorder_RequestStart(void)
{
  if ((AudioRecorder_IsBusy() == 0U) &&
      (SD_Storage_ApplicationAccessAllowed() != 0U))
  {
    Recorder_SendCommand(RECORDER_EVENT_START);
  }
}

void AudioRecorder_RequestStop(void)
{
  if (AudioRecorder_IsBusy() != 0U)
  {
    Recorder_SendCommand(RECORDER_EVENT_STOP);
  }
}

static void Recorder_WriteLe16(uint8_t *data, uint16_t value)
{
  data[0] = (uint8_t)value;
  data[1] = (uint8_t)(value >> 8);
}

static void Recorder_WriteLe32(uint8_t *data, uint32_t value)
{
  data[0] = (uint8_t)value;
  data[1] = (uint8_t)(value >> 8);
  data[2] = (uint8_t)(value >> 16);
  data[3] = (uint8_t)(value >> 24);
}

static void Recorder_MakeWavHeader(uint8_t header[44], uint32_t data_bytes)
{
  memset(header, 0, 44U);
  memcpy(&header[0], "RIFF", 4U);
  Recorder_WriteLe32(&header[4], 36U + data_bytes);
  memcpy(&header[8], "WAVEfmt ", 8U);
  Recorder_WriteLe32(&header[16], 16U);
  Recorder_WriteLe16(&header[20], 1U);
  Recorder_WriteLe16(&header[22], 1U);
  Recorder_WriteLe32(&header[24], RECORDER_SAMPLE_RATE);
  Recorder_WriteLe32(&header[28], RECORDER_SAMPLE_RATE * 2U);
  Recorder_WriteLe16(&header[32], 2U);
  Recorder_WriteLe16(&header[34], 16U);
  memcpy(&header[36], "data", 4U);
  Recorder_WriteLe32(&header[40], data_bytes);
}

static FRESULT Recorder_FindFilename(char path[24], char filename[13])
{
  FILINFO info;
  FRESULT result;
  uint32_t number;

  for (number = 1U; number <= 99999999U; ++number)
  {
    (void)snprintf(filename, 13U, "%lu.wav", (unsigned long)number);
    (void)snprintf(path, 24U, "%s/%s", USERPath, filename);
    result = f_stat(path, &info);
    if (result == FR_NO_FILE)
    {
      return FR_OK;
    }
    if (result != FR_OK)
    {
      return result;
    }
  }
  return FR_DENIED;
}

static void Recorder_Finalize(FIL *file, AudioRecorderSnapshot *snapshot)
{
  uint8_t header[44];
  UINT written;

  recorder_capture_active = 0U;
  (void)BSP_AUDIO_IN_Stop();
  BSP_AUDIO_IN_DeInit();
  Recorder_MakeWavHeader(header, snapshot->data_bytes);
  if ((f_lseek(file, 0U) != FR_OK) ||
      (f_write(file, header, sizeof(header), &written) != FR_OK) ||
      (written != sizeof(header)) ||
      (f_sync(file) != FR_OK))
  {
    ++audio_recorder_error_count;
    snapshot->status = AUDIO_RECORDER_FILESYSTEM_ERROR;
  }
  (void)f_close(file);
}

static void Recorder_QueuePcmHalfFromISR(uint32_t offset)
{
  BaseType_t task_woken = pdFALSE;
  RecorderEvent event;
  int16_t *destination;
  uint32_t sample;
  uint16_t peak = 0U;

  if ((recorder_capture_active == 0U) || (recorder_queue == NULL))
  {
    return;
  }

  event.type = RECORDER_EVENT_AUDIO_CHUNK;
  event.index = recorder_ring_write;
  destination = recorder_ring[event.index];
  for (sample = 0U; sample < RECORDER_MONO_SAMPLES; ++sample)
  {
    uint32_t source = offset + sample * RECORDER_MIC_COUNT;
    int32_t mixed = (int32_t)(int16_t)recorder_pcm[source] +
                    (int32_t)(int16_t)recorder_pcm[source + 1U] +
                    (int32_t)(int16_t)recorder_pcm[source + 2U] +
                    (int32_t)(int16_t)recorder_pcm[source + 3U];
    uint16_t magnitude;
    mixed /= 4;
    destination[sample] = (int16_t)mixed;
    magnitude = (mixed < 0) ? (uint16_t)(-mixed) : (uint16_t)mixed;
    if (magnitude > peak)
    {
      peak = magnitude;
    }
  }

  if (xQueueSendFromISR(recorder_queue, &event, &task_woken) == pdPASS)
  {
    recorder_ring_write = (uint8_t)((recorder_ring_write + 1U) % RECORDER_RING_CHUNKS);
    recorder_snapshot.peak = peak;
  }
  else
  {
    ++recorder_snapshot.dropped_chunks;
    ++audio_recorder_error_count;
  }
  portYIELD_FROM_ISR(task_woken);
}

void BSP_AUDIO_IN_HalfTransfer_CallBack(void)
{
  if (recorder_capture_active != 0U)
  {
    ++audio_recorder_dma_half_count;
    Recorder_QueuePcmHalfFromISR(0U);
  }
  else
  {
    AudioSpectrum_HalfTransferFromISR();
  }
}

void BSP_AUDIO_IN_TransferComplete_CallBack(void)
{
  if (recorder_capture_active != 0U)
  {
    ++audio_recorder_dma_full_count;
    Recorder_QueuePcmHalfFromISR(RECORDER_HALF_HALFWORDS);
  }
  else
  {
    AudioSpectrum_TransferCompleteFromISR();
  }
}

void BSP_AUDIO_IN_Error_CallBack(void)
{
  if (recorder_capture_active != 0U)
  {
    recorder_capture_active = 0U;
    ++audio_recorder_error_count;
  }
  else
  {
    AudioSpectrum_ErrorFromISR();
  }
}

void AudioRecorder_Task(void const *argument)
{
  AudioRecorderSnapshot snapshot;
  RecorderEvent event;
  FIL file;
  uint8_t file_open = 0U;
  uint8_t header[44];
  char path[24];
  UINT written;
  FRESULT result;
  uint32_t last_sync_bytes = 0U;

  (void)argument;
  AudioRecorder_GetSnapshot(&snapshot);

  for (;;)
  {
    if (audio_recorder_debug_command != 0U)
    {
      event.type = (audio_recorder_debug_command == 1U) ?
                   RECORDER_EVENT_START : RECORDER_EVENT_STOP;
      event.index = 0U;
      audio_recorder_debug_command = 0U;
    }
    else if (xQueueReceive(recorder_queue, &event, pdMS_TO_TICKS(100U)) != pdPASS)
    {
      if ((file_open != 0U) && (SD_Card_IsPresent() == 0U))
      {
        event.type = RECORDER_EVENT_STOP;
      }
      else
      {
        continue;
      }
    }

    if (event.type == RECORDER_EVENT_START)
    {
      SD_StorageSnapshot storage;
      if (file_open != 0U)
      {
        continue;
      }
      if ((AudioPlayer_IsBusy() != 0U) ||
          (USB_Audio_ClaimsCodec() != 0U))
      {
        snapshot.status = AUDIO_RECORDER_AUDIO_ERROR;
        Recorder_Publish(&snapshot);
        continue;
      }
      if (AudioSpectrum_IsBusy() != 0U)
      {
        snapshot.status = AUDIO_RECORDER_AUDIO_ERROR;
        Recorder_Publish(&snapshot);
        continue;
      }
      SD_Storage_GetSnapshot(&storage);
      if ((SD_Card_IsPresent() == 0U) || (storage.status == SD_STORAGE_NO_CARD))
      {
        snapshot.status = AUDIO_RECORDER_NO_CARD;
        Recorder_Publish(&snapshot);
        continue;
      }
      if (storage.status != SD_STORAGE_READY)
      {
        snapshot.status = AUDIO_RECORDER_FILESYSTEM_ERROR;
        snapshot.last_fatfs_result = storage.last_result;
        Recorder_Publish(&snapshot);
        continue;
      }

      snapshot.status = AUDIO_RECORDER_STARTING;
      snapshot.data_bytes = 0U;
      snapshot.elapsed_seconds = 0U;
      snapshot.dropped_chunks = 0U;
      snapshot.peak = 0U;
      snapshot.last_fatfs_result = 0U;
      snapshot.filename[0] = 0;
      snapshot.codec_id = audio_codec_id;
      snapshot.codec_ready = audio_codec_ready;
      Recorder_Publish(&snapshot);

      result = Recorder_FindFilename(path, snapshot.filename);
      if (result == FR_OK)
      {
        result = f_open(&file, path, FA_CREATE_NEW | FA_WRITE);
      }
      if (result != FR_OK)
      {
        snapshot.status = AUDIO_RECORDER_FILESYSTEM_ERROR;
        snapshot.last_fatfs_result = (uint8_t)result;
        ++audio_recorder_error_count;
        Recorder_Publish(&snapshot);
        continue;
      }
      file_open = 1U;
      Recorder_MakeWavHeader(header, 0U);
      result = f_write(&file, header, sizeof(header), &written);
      if ((result != FR_OK) || (written != sizeof(header)))
      {
        snapshot.status = AUDIO_RECORDER_FILESYSTEM_ERROR;
        snapshot.last_fatfs_result = (uint8_t)result;
        (void)f_close(&file);
        (void)f_unlink(path);
        file_open = 0U;
        ++audio_recorder_error_count;
        Recorder_Publish(&snapshot);
        continue;
      }

      if ((BSP_AUDIO_IN_Init(RECORDER_SAMPLE_RATE, 16U, RECORDER_MIC_COUNT) != AUDIO_OK) ||
          (BSP_AUDIO_IN_AllocScratch(recorder_scratch, RECORDER_SCRATCH_WORDS) != AUDIO_OK) ||
          (BSP_AUDIO_IN_Record(recorder_pcm, RECORDER_PCM_HALFWORDS) != AUDIO_OK))
      {
        BSP_AUDIO_IN_DeInit();
        (void)f_close(&file);
        (void)f_unlink(path);
        file_open = 0U;
        snapshot.status = AUDIO_RECORDER_AUDIO_ERROR;
        ++audio_recorder_error_count;
        Recorder_Publish(&snapshot);
        continue;
      }

      recorder_ring_write = 0U;
      recorder_capture_active = 1U;
      last_sync_bytes = 0U;
      snapshot.status = AUDIO_RECORDER_RECORDING;
      Recorder_Publish(&snapshot);
    }
    else if (event.type == RECORDER_EVENT_AUDIO_CHUNK)
    {
      if ((file_open == 0U) || (snapshot.status != AUDIO_RECORDER_RECORDING))
      {
        continue;
      }
      result = f_write(&file, recorder_ring[event.index],
                       sizeof(recorder_ring[event.index]), &written);
      if ((result != FR_OK) || (written != sizeof(recorder_ring[event.index])))
      {
        snapshot.status = AUDIO_RECORDER_FILESYSTEM_ERROR;
        snapshot.last_fatfs_result = (uint8_t)result;
        ++audio_recorder_error_count;
        Recorder_Publish(&snapshot);
        event.type = RECORDER_EVENT_STOP;
      }
      else
      {
        ++audio_recorder_write_count;
        snapshot.data_bytes += written;
        snapshot.elapsed_seconds = snapshot.data_bytes /
                                   (RECORDER_SAMPLE_RATE * 2U);
        snapshot.peak = recorder_snapshot.peak;
        snapshot.dropped_chunks = recorder_snapshot.dropped_chunks;
        if ((snapshot.data_bytes - last_sync_bytes) >= RECORDER_SYNC_INTERVAL)
        {
          if (f_sync(&file) == FR_OK)
          {
            last_sync_bytes = snapshot.data_bytes;
          }
        }
        Recorder_Publish(&snapshot);
      }
    }

    if (event.type == RECORDER_EVENT_STOP)
    {
      if (file_open != 0U)
      {
        snapshot.status = AUDIO_RECORDER_STOPPING;
        Recorder_Publish(&snapshot);
        Recorder_Finalize(&file, &snapshot);
        file_open = 0U;
      }
      snapshot.status = (snapshot.status == AUDIO_RECORDER_FILESYSTEM_ERROR) ?
                        AUDIO_RECORDER_FILESYSTEM_ERROR : AUDIO_RECORDER_READY;
      snapshot.peak = 0U;
      Recorder_Publish(&snapshot);
    }
  }
}
