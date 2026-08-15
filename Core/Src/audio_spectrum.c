#include "audio_spectrum.h"

#include "audio_player.h"
#include "audio_recorder.h"
#include "usb_audio.h"
#include "rtc.h"
#include "stm32f769i_discovery_audio.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "arm_math.h"

#include <math.h>
#include <string.h>

/* VSCode Run/Debug builds the rest of the firmware at -O0. Keep this isolated
   DSP translation unit optimized so four FFTs still complete in real time. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize ("O3")
#endif

#define SPECTRUM_SAMPLE_RATE       48000U
#define SPECTRUM_MIC_COUNT         4U
#define SPECTRUM_FFT_SIZE          1024U
#define SPECTRUM_GOERTZEL_SAMPLES  256U
#define SPECTRUM_CAPTURE_SAMPLES   128U
#define SPECTRUM_PCM_HALFWORDS     (SPECTRUM_CAPTURE_SAMPLES * SPECTRUM_MIC_COUNT * 2U)
#define SPECTRUM_HALF_HALFWORDS    (SPECTRUM_PCM_HALFWORDS / 2U)
#define SPECTRUM_FFT_HALFWORDS     (SPECTRUM_FFT_SIZE * SPECTRUM_MIC_COUNT)
#define SPECTRUM_GOERTZEL_HALFWORDS (SPECTRUM_GOERTZEL_SAMPLES * SPECTRUM_MIC_COUNT)
#define SPECTRUM_SCRATCH_WORDS     256U
#define SPECTRUM_TRIGGER_QUEUE_LENGTH 8U
#define SPECTRUM_FFT_QUEUE_LENGTH  12U
#define SPECTRUM_MAX_FREQUENCY_HZ  10000U
#define SPECTRUM_DB_FLOOR          (-90.0f)
#define SPECTRUM_TRIGGER_DEFAULT_HZ 5000U
#define SPECTRUM_TRIGGER_DEFAULT_DB (-300)
#define SPECTRUM_TRIGGER_HYST_DB    60
#define SPECTRUM_TRIGGER_COOLDOWN   (RTC_LSE_TICK_HZ / 4U)
#define SPECTRUM_TRIGGER_QUIET_WINDOWS 32U
#define SPECTRUM_GOERTZEL_SPACING_HZ 125U
#define SPECTRUM_GOERTZEL_PROBE_COUNT 3U
#define SPECTRUM_GOERTZEL_PRESET_COUNT 7U
#define SPECTRUM_NOTIFY_START      (1UL << 0)
#define SPECTRUM_NOTIFY_STOP       (1UL << 1)
#define SPECTRUM_NOTIFY_ERROR      (1UL << 4)
#define SPECTRUM_NOTIFY_DATA       (1UL << 5)
#define SPECTRUM_PI                3.14159265358979323846f

typedef struct
{
  uint32_t rtc_tick;
  uint32_t sequence;
  uint32_t session;
  uint16_t samples[SPECTRUM_HALF_HALFWORDS];
} SpectrumAudioChunk;

volatile uint32_t audio_spectrum_fft_count = 0U;
volatile uint32_t audio_spectrum_goertzel_count = 0U;
volatile uint32_t audio_spectrum_dma_half_count = 0U;
volatile uint32_t audio_spectrum_dma_full_count = 0U;
volatile uint32_t audio_spectrum_error_count = 0U;
volatile uint32_t audio_spectrum_trigger_queue_drop_count = 0U;
volatile uint32_t audio_spectrum_fft_queue_drop_count = 0U;

/* DFSDM DMA cannot access DTCM. Only its four scratch channels need to live
   in the non-cacheable DMA region; the interleaved PCM buffer is filled by
   the BSP callback running on the CPU. */
__attribute__((section(".dma_buffer"), aligned(32)))
static int32_t spectrum_scratch[SPECTRUM_SCRATCH_WORDS];

static uint16_t spectrum_pcm[SPECTRUM_PCM_HALFWORDS];
static uint16_t spectrum_goertzel_pcm[SPECTRUM_GOERTZEL_HALFWORDS];
static uint16_t spectrum_fft_pcm[SPECTRUM_FFT_HALFWORDS];
static float spectrum_real[SPECTRUM_FFT_SIZE];
static float spectrum_imag[SPECTRUM_FFT_SIZE];
static float spectrum_window[SPECTRUM_FFT_SIZE];
static float spectrum_bin_power[SPECTRUM_FFT_SIZE / 2U];
static float spectrum_window_sum;
static arm_rfft_fast_instance_f32 spectrum_rfft;
static uint8_t spectrum_rfft_ready;
static TaskHandle_t spectrum_task_handle;
static QueueHandle_t spectrum_trigger_queue;
static QueueHandle_t spectrum_fft_queue;
static StaticQueue_t spectrum_trigger_queue_control;
static StaticQueue_t spectrum_fft_queue_control;
__attribute__((aligned(4)))
static uint8_t spectrum_trigger_queue_storage[
    SPECTRUM_TRIGGER_QUEUE_LENGTH * sizeof(SpectrumAudioChunk)];
__attribute__((aligned(4)))
static uint8_t spectrum_fft_queue_storage[
    SPECTRUM_FFT_QUEUE_LENGTH * sizeof(SpectrumAudioChunk)];
static SpectrumAudioChunk spectrum_isr_chunk;
static volatile uint8_t spectrum_capture_active;
static volatile uint32_t spectrum_capture_session;
static volatile uint32_t spectrum_capture_sequence;
static AudioSpectrumSnapshot spectrum_snapshot;
static volatile uint32_t spectrum_trigger_frequency_hz = SPECTRUM_TRIGGER_DEFAULT_HZ;
static volatile int16_t spectrum_trigger_threshold_db_tenths = SPECTRUM_TRIGGER_DEFAULT_DB;
/* 2*cos(2*pi*f/48000), with probes at preset-125, preset and preset+125 Hz.
   These are constants for every frequency exposed by the Spectrum UI. */
static const uint16_t spectrum_goertzel_preset_hz[
    SPECTRUM_GOERTZEL_PRESET_COUNT] =
{
  3000U, 4000U, 5000U, 6000U, 7000U, 8000U, 9000U
};
static const float spectrum_goertzel_preset_coefficients
    [SPECTRUM_GOERTZEL_PRESET_COUNT][SPECTRUM_GOERTZEL_PROBE_COUNT] =
{
  {1.8600344474f, 1.8477590650f, 1.8349889929f},
  {1.7481806833f, 1.7320508076f, 1.7154572200f},
  {1.6064150630f, 1.5867066806f, 1.5665734985f},
  {1.4371632356f, 1.4142135624f, 1.3908852700f},
  {1.2433211467f, 1.2175228580f, 1.1913986090f},
  {1.0282054884f, 1.0000000000f, 0.9715267874f},
  {0.7954969491f, 0.7653668647f, 0.7350318732f}
};
static float spectrum_goertzel_coefficients[SPECTRUM_GOERTZEL_PROBE_COUNT] =
{
  1.6064150630f, 1.5867066806f, 1.5665734985f
};
static uint32_t spectrum_stopwatch_start_tick;
static uint32_t spectrum_stopwatch_accumulated_ticks;
static uint32_t spectrum_last_trigger_tick;
static uint32_t spectrum_trigger_count;
static uint8_t spectrum_stopwatch_running;
static uint8_t spectrum_trigger_armed;
static uint8_t spectrum_quiet_frames;
static uint16_t spectrum_fft_fill_samples;

static void Spectrum_SelectGoertzelCoefficients(uint32_t frequency_hz,
                                                 float *coefficients)
{
  uint32_t preset;

  for (preset = 0U; preset < SPECTRUM_GOERTZEL_PRESET_COUNT; ++preset)
  {
    if (frequency_hz == spectrum_goertzel_preset_hz[preset])
    {
      memcpy(coefficients, spectrum_goertzel_preset_coefficients[preset],
             sizeof(spectrum_goertzel_preset_coefficients[preset]));
      return;
    }
  }

  /* Preserve the public API for non-UI frequencies. This slow path runs only
     once when the frequency changes, never in the real-time detector loop. */
  for (uint32_t probe = 0U; probe < SPECTRUM_GOERTZEL_PROBE_COUNT; ++probe)
  {
    const int32_t offset = ((int32_t)probe - 1) *
                           (int32_t)SPECTRUM_GOERTZEL_SPACING_HZ;
    coefficients[probe] = 2.0f * cosf(
        (2.0f * SPECTRUM_PI * (float)((int32_t)frequency_hz + offset)) /
        (float)SPECTRUM_SAMPLE_RATE);
  }
}

static void Spectrum_Publish(AudioSpectrumSnapshot *snapshot)
{
  taskENTER_CRITICAL();
  /* FFT/status and trigger data are produced by different tasks. Preserve
     the high-priority trigger task's latest fields during an FFT publish. */
  snapshot->trigger_frequency_hz = spectrum_snapshot.trigger_frequency_hz;
  snapshot->trigger_threshold_db_tenths =
      spectrum_snapshot.trigger_threshold_db_tenths;
  snapshot->trigger_level_db_tenths = spectrum_snapshot.trigger_level_db_tenths;
  snapshot->stopwatch_ticks = spectrum_snapshot.stopwatch_ticks;
  snapshot->trigger_count = spectrum_snapshot.trigger_count;
  snapshot->stopwatch_running = spectrum_snapshot.stopwatch_running;
  snapshot->trigger_armed = spectrum_snapshot.trigger_armed;
  snapshot->generation = spectrum_snapshot.generation + 1U;
  spectrum_snapshot = *snapshot;
  taskEXIT_CRITICAL();
}

void AudioSpectrum_GetSnapshot(AudioSpectrumSnapshot *snapshot)
{
  if (snapshot == NULL) return;
  taskENTER_CRITICAL();
  *snapshot = spectrum_snapshot;
  taskEXIT_CRITICAL();
}

void AudioSpectrum_SetTriggerFrequency(uint32_t frequency_hz)
{
  float coefficients[SPECTRUM_GOERTZEL_PROBE_COUNT];

  if (frequency_hz < 100U) frequency_hz = 100U;
  if (frequency_hz > SPECTRUM_MAX_FREQUENCY_HZ)
    frequency_hz = SPECTRUM_MAX_FREQUENCY_HZ;
  Spectrum_SelectGoertzelCoefficients(frequency_hz, coefficients);

  taskENTER_CRITICAL();
  spectrum_trigger_frequency_hz = frequency_hz;
  memcpy(spectrum_goertzel_coefficients, coefficients, sizeof(coefficients));
  spectrum_trigger_armed = 0U;
  spectrum_quiet_frames = 0U;
  spectrum_last_trigger_tick = RTC_LSE_GetTicks();
  spectrum_snapshot.trigger_frequency_hz = frequency_hz;
  spectrum_snapshot.trigger_armed = 0U;
  ++spectrum_snapshot.generation;
  taskEXIT_CRITICAL();
}

void AudioSpectrum_SetTriggerThreshold(int16_t threshold_db_tenths)
{
  if (threshold_db_tenths > -10) threshold_db_tenths = -10;
  if (threshold_db_tenths < -900) threshold_db_tenths = -900;

  taskENTER_CRITICAL();
  spectrum_trigger_threshold_db_tenths = threshold_db_tenths;
  spectrum_trigger_armed = 0U;
  spectrum_quiet_frames = 0U;
  spectrum_last_trigger_tick = RTC_LSE_GetTicks();
  spectrum_snapshot.trigger_threshold_db_tenths = threshold_db_tenths;
  spectrum_snapshot.trigger_armed = 0U;
  ++spectrum_snapshot.generation;
  taskEXIT_CRITICAL();
}

void AudioSpectrum_ResetTriggerStopwatch(void)
{
  taskENTER_CRITICAL();
  spectrum_stopwatch_running = 0U;
  spectrum_stopwatch_start_tick = RTC_LSE_GetTicks();
  spectrum_stopwatch_accumulated_ticks = 0U;
  spectrum_trigger_count = 0U;
  spectrum_snapshot.stopwatch_ticks = 0U;
  spectrum_snapshot.stopwatch_running = 0U;
  spectrum_snapshot.trigger_count = 0U;
  ++spectrum_snapshot.generation;
  taskEXIT_CRITICAL();
}

void AudioSpectrum_CreateResources(void)
{
  uint32_t index;
  memset(&spectrum_snapshot, 0, sizeof(spectrum_snapshot));
  spectrum_snapshot.status = AUDIO_SPECTRUM_STOPPED;
  spectrum_snapshot.sample_rate = SPECTRUM_SAMPLE_RATE;
  spectrum_snapshot.peak_db_tenths = -900;
  spectrum_snapshot.trigger_frequency_hz = SPECTRUM_TRIGGER_DEFAULT_HZ;
  spectrum_snapshot.trigger_threshold_db_tenths = SPECTRUM_TRIGGER_DEFAULT_DB;
  spectrum_snapshot.trigger_level_db_tenths = -900;
  for (index = 0U; index < AUDIO_SPECTRUM_BAND_COUNT; ++index)
  {
    spectrum_snapshot.bands_db_tenths[index] = -900;
  }

  spectrum_window_sum = 0.0f;
  for (index = 0U; index < SPECTRUM_FFT_SIZE; ++index)
  {
    spectrum_window[index] = 0.5f - 0.5f *
        cosf((2.0f * SPECTRUM_PI * (float)index) /
             (float)(SPECTRUM_FFT_SIZE - 1U));
    spectrum_window_sum += spectrum_window[index];
  }
  spectrum_rfft_ready =
      (arm_rfft_fast_init_f32(&spectrum_rfft, SPECTRUM_FFT_SIZE) == ARM_MATH_SUCCESS) ?
      1U : 0U;

  spectrum_trigger_queue = xQueueCreateStatic(
      SPECTRUM_TRIGGER_QUEUE_LENGTH, sizeof(SpectrumAudioChunk),
      spectrum_trigger_queue_storage, &spectrum_trigger_queue_control);
  spectrum_fft_queue = xQueueCreateStatic(
      SPECTRUM_FFT_QUEUE_LENGTH, sizeof(SpectrumAudioChunk),
      spectrum_fft_queue_storage, &spectrum_fft_queue_control);
}

uint8_t AudioSpectrum_IsBusy(void)
{
  AudioSpectrumStatus status = spectrum_snapshot.status;
  return ((spectrum_capture_active != 0U) ||
          (status == AUDIO_SPECTRUM_STARTING) ||
          (status == AUDIO_SPECTRUM_RUNNING)) ? 1U : 0U;
}

void AudioSpectrum_RequestStart(void)
{
  if (spectrum_task_handle != NULL)
  {
    (void)xTaskNotify(spectrum_task_handle, SPECTRUM_NOTIFY_START, eSetBits);
  }
}

void AudioSpectrum_RequestStop(void)
{
  if (spectrum_task_handle != NULL)
  {
    (void)xTaskNotify(spectrum_task_handle, SPECTRUM_NOTIFY_STOP, eSetBits);
  }
}

static void Spectrum_NotifyFromISR(uint32_t bit)
{
  BaseType_t task_woken = pdFALSE;
  if ((spectrum_capture_active == 0U) || (spectrum_task_handle == NULL)) return;
  (void)xTaskNotifyFromISR(spectrum_task_handle, bit, eSetBits, &task_woken);
  portYIELD_FROM_ISR(task_woken);
}

static void Spectrum_QueueChunkFromISR(uint32_t offset)
{
  BaseType_t task_woken = pdFALSE;
  if ((spectrum_capture_active == 0U) ||
      (spectrum_trigger_queue == NULL) || (spectrum_fft_queue == NULL))
  {
    return;
  }

  spectrum_isr_chunk.rtc_tick = RTC_LSE_GetTicks();
  spectrum_isr_chunk.sequence = ++spectrum_capture_sequence;
  spectrum_isr_chunk.session = spectrum_capture_session;
  memcpy(spectrum_isr_chunk.samples, &spectrum_pcm[offset],
         sizeof(spectrum_isr_chunk.samples));

  if (xQueueSendFromISR(spectrum_trigger_queue, &spectrum_isr_chunk,
                        &task_woken) != pdPASS)
  {
    ++audio_spectrum_trigger_queue_drop_count;
  }
  if (xQueueSendFromISR(spectrum_fft_queue, &spectrum_isr_chunk,
                        &task_woken) != pdPASS)
  {
    ++audio_spectrum_fft_queue_drop_count;
  }
  if (spectrum_task_handle != NULL)
  {
    (void)xTaskNotifyFromISR(spectrum_task_handle, SPECTRUM_NOTIFY_DATA,
                            eSetBits, &task_woken);
  }
  portYIELD_FROM_ISR(task_woken);
}

void AudioSpectrum_HalfTransferFromISR(void)
{
  if (spectrum_capture_active == 0U) return;
  ++audio_spectrum_dma_half_count;
  Spectrum_QueueChunkFromISR(0U);
}

void AudioSpectrum_TransferCompleteFromISR(void)
{
  if (spectrum_capture_active == 0U) return;
  ++audio_spectrum_dma_full_count;
  Spectrum_QueueChunkFromISR(SPECTRUM_HALF_HALFWORDS);
}

void AudioSpectrum_ErrorFromISR(void)
{
  if (spectrum_capture_active == 0U) return;
  ++audio_spectrum_error_count;
  Spectrum_NotifyFromISR(SPECTRUM_NOTIFY_ERROR);
}

static float Spectrum_PowerDb(float power)
{
  const float magnitude = sqrtf(power / (float)SPECTRUM_MIC_COUNT);
  const float amplitude = (2.0f * magnitude) /
                          (spectrum_window_sum * 32768.0f);
  float db = (amplitude > 0.0000316228f) ? 20.0f * log10f(amplitude) :
                                          SPECTRUM_DB_FLOOR;
  if (db > 0.0f) db = 0.0f;
  if (db < SPECTRUM_DB_FLOOR) db = SPECTRUM_DB_FLOOR;
  return db;
}

static int16_t Spectrum_GoertzelDb(const uint16_t *samples)
{
  int16_t best_db_tenths = -900;
  float coefficients[SPECTRUM_GOERTZEL_PROBE_COUNT];

  taskENTER_CRITICAL();
  memcpy(coefficients, spectrum_goertzel_coefficients, sizeof(coefficients));
  taskEXIT_CRITICAL();

  /* Three closely spaced probes retain the old narrow-band tolerance while
     requiring only 12 scalar resonators (3 frequencies x 4 microphones). */
  for (uint32_t probe = 0U; probe < SPECTRUM_GOERTZEL_PROBE_COUNT; ++probe)
  {
    const float coefficient = coefficients[probe];
    float state1[SPECTRUM_MIC_COUNT] = {0.0f};
    float state2[SPECTRUM_MIC_COUNT] = {0.0f};

    for (uint32_t sample = 0U; sample < SPECTRUM_GOERTZEL_SAMPLES; ++sample)
    {
      for (uint32_t microphone = 0U; microphone < SPECTRUM_MIC_COUNT; ++microphone)
      {
        const float input = (float)(int16_t)samples[
            sample * SPECTRUM_MIC_COUNT + microphone];
        const float state0 = input + coefficient * state1[microphone] -
                             state2[microphone];
        state2[microphone] = state1[microphone];
        state1[microphone] = state0;
      }
    }

    float total_power = 0.0f;
    for (uint32_t microphone = 0U; microphone < SPECTRUM_MIC_COUNT; ++microphone)
    {
      float power = state1[microphone] * state1[microphone] +
                    state2[microphone] * state2[microphone] -
                    coefficient * state1[microphone] * state2[microphone];
      if (power < 0.0f) power = 0.0f;
      total_power += power;
    }

    const float magnitude = sqrtf(total_power / (float)SPECTRUM_MIC_COUNT);
    const float amplitude = (2.0f * magnitude) /
        ((float)SPECTRUM_GOERTZEL_SAMPLES * 32768.0f);
    float db = amplitude > 0.0000316228f ? 20.0f * log10f(amplitude) :
                                          SPECTRUM_DB_FLOOR;
    if (db > 0.0f) db = 0.0f;
    if (db < SPECTRUM_DB_FLOOR) db = SPECTRUM_DB_FLOOR;
    const int16_t db_tenths = (int16_t)(db * 10.0f);
    if (db_tenths > best_db_tenths) best_db_tenths = db_tenths;
  }

  ++audio_spectrum_goertzel_count;
  return best_db_tenths;
}

static void Spectrum_UpdateTrigger(int16_t level_db_tenths,
                                   uint32_t trigger_tick)
{
  uint32_t now;
  int16_t threshold;

  /* The GUI can change sensitivity or reset at any time. Keep each tone
     decision atomic so a reset can never be undone by a pre-empted frame. */
  taskENTER_CRITICAL();
  now = RTC_LSE_GetTicks();
  threshold = spectrum_trigger_threshold_db_tenths;

  if (spectrum_trigger_armed == 0U)
  {
    if (level_db_tenths <= (threshold - SPECTRUM_TRIGGER_HYST_DB))
    {
      if (spectrum_quiet_frames < SPECTRUM_TRIGGER_QUIET_WINDOWS)
        ++spectrum_quiet_frames;
    }
    else
    {
      spectrum_quiet_frames = 0U;
    }
    if ((spectrum_quiet_frames >= SPECTRUM_TRIGGER_QUIET_WINDOWS) &&
        ((int32_t)(trigger_tick - spectrum_last_trigger_tick) >=
         (int32_t)SPECTRUM_TRIGGER_COOLDOWN))
    {
      spectrum_trigger_armed = 1U;
    }
  }
  else if (level_db_tenths >= threshold)
  {
    if (spectrum_stopwatch_running == 0U)
    {
      spectrum_stopwatch_start_tick = trigger_tick;
      spectrum_stopwatch_running = 1U;
    }
    else
    {
      spectrum_stopwatch_accumulated_ticks +=
          (uint32_t)(trigger_tick - spectrum_stopwatch_start_tick);
      spectrum_stopwatch_running = 0U;
    }
    ++spectrum_trigger_count;
    spectrum_last_trigger_tick = trigger_tick;
    spectrum_trigger_armed = 0U;
    spectrum_quiet_frames = 0U;
  }

  spectrum_snapshot.trigger_frequency_hz = spectrum_trigger_frequency_hz;
  spectrum_snapshot.trigger_threshold_db_tenths = threshold;
  spectrum_snapshot.trigger_level_db_tenths = level_db_tenths;
  spectrum_snapshot.stopwatch_running = spectrum_stopwatch_running;
  spectrum_snapshot.trigger_armed = spectrum_trigger_armed;
  spectrum_snapshot.trigger_count = spectrum_trigger_count;
  spectrum_snapshot.stopwatch_ticks = spectrum_stopwatch_accumulated_ticks;
  if (spectrum_stopwatch_running != 0U)
  {
    spectrum_snapshot.stopwatch_ticks +=
        (uint32_t)(now - spectrum_stopwatch_start_tick);
  }
  ++spectrum_snapshot.generation;
  taskEXIT_CRITICAL();
}

static void Spectrum_ResetTriggerDetector(uint32_t tick)
{
  taskENTER_CRITICAL();
  spectrum_trigger_armed = 0U;
  spectrum_quiet_frames = 0U;
  /* A new stream or a recovered queue gap needs the normal quiet-window
     qualification, but not an additional artificial 250 ms startup delay. */
  spectrum_last_trigger_tick = tick - SPECTRUM_TRIGGER_COOLDOWN;
  spectrum_snapshot.trigger_armed = 0U;
  ++spectrum_snapshot.generation;
  taskEXIT_CRITICAL();
}

static uint8_t Spectrum_AppendFftChunk(const uint16_t *samples)
{
  memcpy(&spectrum_fft_pcm[spectrum_fft_fill_samples * SPECTRUM_MIC_COUNT],
         samples, SPECTRUM_HALF_HALFWORDS * sizeof(samples[0]));
  spectrum_fft_fill_samples = (uint16_t)(spectrum_fft_fill_samples +
                                         SPECTRUM_CAPTURE_SAMPLES);
  if (spectrum_fft_fill_samples < SPECTRUM_FFT_SIZE) return 0U;
  spectrum_fft_fill_samples = 0U;
  return 1U;
}

static void Spectrum_ProcessFft(AudioSpectrumSnapshot *snapshot)
{
  uint32_t index;
  uint32_t peak_bin = 1U;
  float peak_db = SPECTRUM_DB_FLOOR;
  const uint32_t max_bin =
      (SPECTRUM_MAX_FREQUENCY_HZ * SPECTRUM_FFT_SIZE) / SPECTRUM_SAMPLE_RATE;

  /* Average the four power spectra, rather than averaging time samples.
     This prevents microphone spacing/phase differences from cancelling
     legitimate high-frequency content in the combined display. */
  memset(spectrum_bin_power, 0, sizeof(spectrum_bin_power));
  for (uint32_t microphone = 0U; microphone < SPECTRUM_MIC_COUNT; ++microphone)
  {
    int64_t sum = 0;
    for (index = 0U; index < SPECTRUM_FFT_SIZE; ++index)
    {
      const int16_t sample = (int16_t)spectrum_fft_pcm[
          index * SPECTRUM_MIC_COUNT + microphone];
      spectrum_real[index] = (float)sample;
      sum += sample;
    }
    const float mean = (float)sum / (float)SPECTRUM_FFT_SIZE;
    for (index = 0U; index < SPECTRUM_FFT_SIZE; ++index)
    {
      spectrum_real[index] = (spectrum_real[index] - mean) * spectrum_window[index];
    }
    /* CMSIS real FFT returns packed bins: DC at [0], Nyquist at [1], then
       real/imaginary pairs for bins 1..N/2-1. */
    arm_rfft_fast_f32(&spectrum_rfft, spectrum_real, spectrum_imag, 0U);
    for (index = 1U; index <= max_bin; ++index)
    {
      const float real = spectrum_imag[index * 2U];
      const float imag = spectrum_imag[index * 2U + 1U];
      spectrum_bin_power[index] +=
          real * real + imag * imag;
    }
  }

  for (uint32_t band = 0U; band < AUDIO_SPECTRUM_BAND_COUNT; ++band)
  {
    uint32_t first = 1U + (band * (max_bin - 1U)) /
                           AUDIO_SPECTRUM_BAND_COUNT;
    uint32_t last = 1U + ((band + 1U) * (max_bin - 1U)) /
                          AUDIO_SPECTRUM_BAND_COUNT;
    float band_db = SPECTRUM_DB_FLOOR;
    if (last <= first) last = first + 1U;
    for (uint32_t bin = first; bin < last && bin <= max_bin; ++bin)
    {
      const float db = Spectrum_PowerDb(spectrum_bin_power[bin]);
      if (db > band_db) band_db = db;
      if (db > peak_db)
      {
        peak_db = db;
        peak_bin = bin;
      }
    }

    const float previous = (float)snapshot->bands_db_tenths[band] / 10.0f;
    const float blend = band_db > previous ? 0.45f : 0.12f;
    const float smoothed = previous + (band_db - previous) * blend;
    snapshot->bands_db_tenths[band] = (int16_t)(smoothed * 10.0f);
  }

  snapshot->peak_frequency_hz =
      (peak_bin * SPECTRUM_SAMPLE_RATE) / SPECTRUM_FFT_SIZE;
  snapshot->peak_db_tenths = (int16_t)(peak_db * 10.0f);

  ++audio_spectrum_fft_count;
}

void AudioSpectrum_TriggerTask(void const *argument)
{
  SpectrumAudioChunk chunk;
  uint32_t active_session = 0U;
  uint32_t last_sequence = 0U;
  uint8_t have_first_half = 0U;
  (void)argument;

  for (;;)
  {
    if ((spectrum_trigger_queue == NULL) ||
        (xQueueReceive(spectrum_trigger_queue, &chunk, portMAX_DELAY) != pdPASS))
    {
      continue;
    }

    if ((spectrum_capture_active == 0U) ||
        (chunk.session != spectrum_capture_session))
    {
      continue;
    }

    if ((chunk.session != active_session) ||
        ((last_sequence != 0U) && (chunk.sequence != last_sequence + 1U)))
    {
      active_session = chunk.session;
      have_first_half = 0U;
      Spectrum_ResetTriggerDetector(chunk.rtc_tick);
    }
    last_sequence = chunk.sequence;

    if (have_first_half == 0U)
    {
      memcpy(spectrum_goertzel_pcm, chunk.samples,
             sizeof(chunk.samples));
      have_first_half = 1U;
      continue;
    }

    memcpy(&spectrum_goertzel_pcm[SPECTRUM_HALF_HALFWORDS], chunk.samples,
           sizeof(chunk.samples));
    const int16_t trigger_level = Spectrum_GoertzelDb(spectrum_goertzel_pcm);
    if ((spectrum_capture_active != 0U) &&
        (chunk.session == spectrum_capture_session))
    {
      Spectrum_UpdateTrigger(trigger_level, chunk.rtc_tick);
    }

    /* Retain the newest 128 samples so the next 256-sample analysis window
       overlaps this one by 50 percent. */
    memcpy(spectrum_goertzel_pcm,
           &spectrum_goertzel_pcm[SPECTRUM_HALF_HALFWORDS],
           sizeof(chunk.samples));
  }
}

static void Spectrum_StopHardware(void)
{
  spectrum_capture_active = 0U;
  (void)BSP_AUDIO_IN_Stop();
  BSP_AUDIO_IN_DeInit();
}

void AudioSpectrum_Task(void const *argument)
{
  AudioSpectrumSnapshot snapshot;
  SpectrumAudioChunk chunk;
  uint32_t notify_bits;
  uint32_t fft_session = 0U;
  uint32_t fft_last_sequence = 0U;
  uint8_t fft_window_divider = 0U;
  (void)argument;
  spectrum_task_handle = xTaskGetCurrentTaskHandle();
  AudioSpectrum_GetSnapshot(&snapshot);

  for (;;)
  {
    notify_bits = 0U;
    (void)xTaskNotifyWait(0U, 0xFFFFFFFFUL, &notify_bits, portMAX_DELAY);

    if ((notify_bits & SPECTRUM_NOTIFY_STOP) != 0U)
    {
      if (spectrum_capture_active != 0U) Spectrum_StopHardware();
      if (spectrum_trigger_queue != NULL) (void)xQueueReset(spectrum_trigger_queue);
      if (spectrum_fft_queue != NULL) (void)xQueueReset(spectrum_fft_queue);
      snapshot.status = AUDIO_SPECTRUM_STOPPED;
      snapshot.peak_frequency_hz = 0U;
      snapshot.peak_db_tenths = -900;
      Spectrum_Publish(&snapshot);
      continue;
    }

    if ((notify_bits & SPECTRUM_NOTIFY_START) != 0U)
    {
      if (spectrum_capture_active != 0U) continue;
      if ((AudioRecorder_IsBusy() != 0U) ||
          (AudioPlayer_IsBusy() != 0U) ||
          (USB_Audio_ClaimsCodec() != 0U))
      {
        snapshot.status = AUDIO_SPECTRUM_BUSY;
        Spectrum_Publish(&snapshot);
        continue;
      }
      snapshot.status = AUDIO_SPECTRUM_STARTING;
      snapshot.dropped_frames = 0U;
      Spectrum_Publish(&snapshot);
      if ((spectrum_rfft_ready == 0U) ||
          (spectrum_trigger_queue == NULL) || (spectrum_fft_queue == NULL) ||
          (BSP_AUDIO_IN_Init(SPECTRUM_SAMPLE_RATE, 16U, SPECTRUM_MIC_COUNT) != AUDIO_OK) ||
          (BSP_AUDIO_IN_AllocScratch(spectrum_scratch, SPECTRUM_SCRATCH_WORDS) != AUDIO_OK))
      {
        BSP_AUDIO_IN_DeInit();
        snapshot.status = AUDIO_SPECTRUM_ERROR;
        ++audio_spectrum_error_count;
        Spectrum_Publish(&snapshot);
        continue;
      }
      (void)xQueueReset(spectrum_trigger_queue);
      (void)xQueueReset(spectrum_fft_queue);
      audio_spectrum_trigger_queue_drop_count = 0U;
      audio_spectrum_fft_queue_drop_count = 0U;
      spectrum_capture_sequence = 0U;
      ++spectrum_capture_session;
      if (spectrum_capture_session == 0U) ++spectrum_capture_session;
      spectrum_capture_active = 1U;
      if (BSP_AUDIO_IN_Record(spectrum_pcm, SPECTRUM_PCM_HALFWORDS) != AUDIO_OK)
      {
        Spectrum_StopHardware();
        snapshot.status = AUDIO_SPECTRUM_ERROR;
        ++audio_spectrum_error_count;
        Spectrum_Publish(&snapshot);
        continue;
      }
      fft_window_divider = 0U;
      spectrum_fft_fill_samples = 0U;
      fft_session = spectrum_capture_session;
      fft_last_sequence = 0U;
      snapshot.status = AUDIO_SPECTRUM_RUNNING;
      Spectrum_Publish(&snapshot);
    }

    if ((notify_bits & SPECTRUM_NOTIFY_ERROR) != 0U)
    {
      if (spectrum_capture_active != 0U) Spectrum_StopHardware();
      if (spectrum_trigger_queue != NULL) (void)xQueueReset(spectrum_trigger_queue);
      if (spectrum_fft_queue != NULL) (void)xQueueReset(spectrum_fft_queue);
      snapshot.status = AUDIO_SPECTRUM_ERROR;
      ++audio_spectrum_error_count;
      Spectrum_Publish(&snapshot);
      continue;
    }

    if ((spectrum_capture_active != 0U) &&
        ((notify_bits & SPECTRUM_NOTIFY_DATA) != 0U))
    {
      /* Drain timestamped chunks in order. The display path may fall behind
         without delaying the independent high-priority trigger task. */
      while (xQueueReceive(spectrum_fft_queue, &chunk, 0U) == pdPASS)
      {
        if ((spectrum_capture_active == 0U) ||
            (chunk.session != spectrum_capture_session))
        {
          continue;
        }
        if ((chunk.session != fft_session) ||
            ((fft_last_sequence != 0U) &&
             (chunk.sequence != fft_last_sequence + 1U)))
        {
          fft_session = chunk.session;
          spectrum_fft_fill_samples = 0U;
          fft_window_divider = 0U;
        }
        fft_last_sequence = chunk.sequence;

        if (Spectrum_AppendFftChunk(chunk.samples) != 0U)
        {
          fft_window_divider ^= 1U;
          if (fft_window_divider == 0U)
          {
            uint32_t dropped = audio_spectrum_trigger_queue_drop_count +
                               audio_spectrum_fft_queue_drop_count;
            if (dropped > 65535U) dropped = 65535U;
            snapshot.dropped_frames = (uint16_t)dropped;
            Spectrum_ProcessFft(&snapshot);
            Spectrum_Publish(&snapshot);
          }
        }
      }
    }
  }
}
