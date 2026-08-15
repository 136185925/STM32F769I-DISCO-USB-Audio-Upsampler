#include "spdif_tx.h"

#include "main.h"

/* DMA2 Stream4 / Channel3 is the ST reference mapping for SAI2 Block A.
 * The active microphone recorder uses DFSDM streams 0/5/6/7, so there is no
 * runtime overlap with this output stream. */
#define SPDIF_TX_DMA_STREAM       DMA2_Stream4
#define SPDIF_TX_DMA_CHANNEL      DMA_CHANNEL_3
#define SPDIF_TX_DMA_IRQn         DMA2_Stream4_IRQn
#define SPDIF_TX_IRQ_PRIORITY     6U

static SAI_HandleTypeDef spdif_tx_sai;
static DMA_HandleTypeDef spdif_tx_dma;
static uint16_t spdif_tx_sample_bytes;
static volatile uint8_t spdif_tx_initialized;
static volatile uint8_t spdif_tx_running;

static uint8_t SPDIF_TX_ClockConfig(uint32_t sample_rate)
{
  RCC_PeriphCLKInitTypeDef clock = {0};
  HAL_RCCEx_GetPeriphCLKConfig(&clock);
  clock.PeriphClockSelection = RCC_PERIPHCLK_SAI2;
  clock.Sai2ClockSelection = RCC_SAI2CLKSOURCE_PLLI2S;
  if ((sample_rate == 11025U) || (sample_rate == 22050U) ||
      (sample_rate == 44100U))
  {
    /* 22.583333 MHz permits the integer S/PDIF MCKDIV sequence. */
    clock.PLLI2S.PLLI2SN = 271U;
    clock.PLLI2S.PLLI2SQ = 2U;
    clock.PLLI2SDivQ = 6U;
  }
  else if (sample_rate == 176400U)
  {
    /* 90.333333 MHz -> 176.432 kHz S/PDIF (+183 ppm). */
    clock.PLLI2S.PLLI2SN = 271U;
    clock.PLLI2S.PLLI2SQ = 3U;
    clock.PLLI2SDivQ = 1U;
  }
  else if (sample_rate == 192000U)
  {
    /* 98.333333 MHz -> 192.057 kHz S/PDIF (+298 ppm). */
    clock.PLLI2S.PLLI2SN = 295U;
    clock.PLLI2S.PLLI2SQ = 3U;
    clock.PLLI2SDivQ = 1U;
  }
  else
  {
    clock.PLLI2S.PLLI2SN = 344U;
    clock.PLLI2S.PLLI2SQ = 7U;
    clock.PLLI2SDivQ = 1U;
  }
  return (HAL_RCCEx_PeriphCLKConfig(&clock) == HAL_OK) ? 1U : 0U;
}

static uint32_t SPDIF_TX_MasterDivider(uint32_t sample_rate)
{
  /* In S/PDIF mode the SAI bit clock must be twice the encoded symbol rate.
   * HAL_SAI_Init() therefore right-shifts the normal PCM MCKDIV once when it
   * calculates the divider automatically. AudioFrequency=MCKDIV selects
   * manual mode, so apply that protocol-specific divide-by-two here as well.
   * Supplying the normal PCM divider made SAI2 consume exactly Fs/2. */
  switch (sample_rate)
  {
    case 8000U:  return 6U;
    case 11025U: return 2U;
    case 16000U: return 3U;
    case 22050U: return 1U;
    case 32000U: return 1U;
    case 44100U: return 0U;
    case 48000U: return 1U;
    case 96000U: return 0U;
    case 176400U:return 0U;
    case 192000U:return 0U;
    default:     return 0U;
  }
}

static uint8_t SPDIF_TX_SampleRateStatus(uint32_t sample_rate)
{
  /* IEC60958-3 consumer channel-status byte 3. Rates without a legacy code
     are marked "not indicated" rather than advertising a wrong rate. */
  switch (sample_rate)
  {
    case 22050U:  return 0x04U;
    case 32000U:  return 0x03U;
    case 44100U:  return 0x00U;
    case 48000U:  return 0x02U;
    case 88200U:  return 0x08U;
    case 96000U:  return 0x0AU;
    case 176400U: return 0x0CU;
    case 192000U: return 0x0EU;
    default:      return 0x01U;
  }
}

uint32_t SPDIF_TX_EncodeSample(int32_t sample, uint16_t bits_per_sample,
                               uint32_t sample_rate,
                               uint32_t channel_status_frame)
{
  uint32_t audio;
  uint32_t control = 0U;
  const uint32_t frame = channel_status_frame % 192U;

  /* IEC60958 transmits shorter PCM words MSB-aligned in the 24-bit audio
     field. V=0 marks valid PCM and U=0. The SAI hardware adds preamble and
     parity, but software must provide the C bit for every subframe. */
  if (bits_per_sample == 16U)
  {
    audio = ((uint32_t)(uint16_t)sample << 8U) & 0x00FFFFFFUL;
  }
  else
  {
    audio = (uint32_t)sample & 0x00FFFFFFUL;
  }

  if ((frame >= 24U) && (frame < 32U))
  {
    const uint8_t status = SPDIF_TX_SampleRateStatus(sample_rate);
    if ((status & (uint8_t)(1U << (frame - 24U))) != 0U)
    {
      control = (1UL << 26U); /* Channel Status (C). */
    }
  }
  return audio | control;
}

uint8_t SPDIF_TX_Init(uint32_t sample_rate, uint16_t bits_per_sample)
{
  GPIO_InitTypeDef gpio = {0};
  HAL_StatusTypeDef hal_status;

  if (((sample_rate != 8000U) && (sample_rate != 11025U) &&
       (sample_rate != 16000U) && (sample_rate != 22050U) &&
       (sample_rate != 32000U) && (sample_rate != 44100U) &&
       (sample_rate != 48000U) && (sample_rate != 96000U) &&
       (sample_rate != 176400U) && (sample_rate != 192000U)) ||
      ((bits_per_sample != 16U) && (bits_per_sample != 24U)))
  {
    return 0U;
  }

  SPDIF_TX_DeInit();

  if (SPDIF_TX_ClockConfig(sample_rate) == 0U)
  {
    return 0U;
  }

  /* S/PDIF owns PLLI2S while selected; WM8994 and SAI1 are mutually exclusive. */
  __HAL_RCC_SAI2_CONFIG(RCC_SAI2CLKSOURCE_PLLI2S);
  __HAL_RCC_SAI2_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_DMA2_CLK_ENABLE();

  gpio.Pin = SPDIF_TX_Pin;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF10_SAI2;
  HAL_GPIO_Init(SPDIF_TX_GPIO_Port, &gpio);

  spdif_tx_sai.Instance = SAI2_Block_A;
  spdif_tx_sai.Init.AudioMode = SAI_MODEMASTER_TX;
  spdif_tx_sai.Init.Synchro = SAI_ASYNCHRONOUS;
  spdif_tx_sai.Init.SynchroExt = SAI_SYNCEXT_DISABLE;
  spdif_tx_sai.Init.OutputDrive = SAI_OUTPUTDRIVE_DISABLE;
  spdif_tx_sai.Init.NoDivider = SAI_MASTERDIVIDER_ENABLED;
  spdif_tx_sai.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_EMPTY;
  /* Use an explicit divider to bypass HAL's long-standing S/PDIF divider
   * calculation issue.  The actual audio rate is still set by PLLI2S and is
   * shared with SAI1; SAI_AUDIO_FREQUENCY_MCKDIV only selects manual mode. */
  spdif_tx_sai.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_MCKDIV;
  spdif_tx_sai.Init.Mckdiv = SPDIF_TX_MasterDivider(sample_rate);
  spdif_tx_sai.Init.MonoStereoMode = SAI_STEREOMODE;
  spdif_tx_sai.Init.CompandingMode = SAI_NOCOMPANDING;
  spdif_tx_sai.Init.TriState = SAI_OUTPUT_NOTRELEASED;
  spdif_tx_sai.Init.Protocol = SAI_SPDIF_PROTOCOL;
  spdif_tx_sai.Init.DataSize = SAI_DATASIZE_24;
  spdif_tx_sai.Init.FirstBit = SAI_FIRSTBIT_MSB;
  spdif_tx_sai.Init.ClockStrobing = SAI_CLOCKSTROBING_FALLINGEDGE;
  spdif_tx_sai.FrameInit.FrameLength = 64U;
  spdif_tx_sai.FrameInit.ActiveFrameLength = 32U;
  spdif_tx_sai.FrameInit.FSDefinition = SAI_FS_STARTFRAME;
  spdif_tx_sai.FrameInit.FSPolarity = SAI_FS_ACTIVE_LOW;
  spdif_tx_sai.FrameInit.FSOffset = SAI_FS_FIRSTBIT;
  spdif_tx_sai.SlotInit.FirstBitOffset = 0U;
  spdif_tx_sai.SlotInit.SlotSize = SAI_SLOTSIZE_DATASIZE;
  spdif_tx_sai.SlotInit.SlotNumber = 4U;
  spdif_tx_sai.SlotInit.SlotActive = SAI_SLOTACTIVE_ALL;

  hal_status = HAL_SAI_Init(&spdif_tx_sai);
  if (hal_status != HAL_OK)
  {
    SPDIF_TX_DeInit();
    return 0U;
  }

  /* HAL_SAI_MspInit applies CubeMX's low-speed GPIO default. Restore the
     edge rate used by ST's S/PDIF reference implementation. */
  HAL_GPIO_Init(SPDIF_TX_GPIO_Port, &gpio);

  spdif_tx_dma.Instance = SPDIF_TX_DMA_STREAM;
  spdif_tx_dma.Init.Channel = SPDIF_TX_DMA_CHANNEL;
  spdif_tx_dma.Init.Direction = DMA_MEMORY_TO_PERIPH;
  spdif_tx_dma.Init.PeriphInc = DMA_PINC_DISABLE;
  spdif_tx_dma.Init.MemInc = DMA_MINC_ENABLE;
  /* S/PDIF mode always consumes a complete 32-bit SAI data-register word:
     24 audio bits plus V/U/C. Half-word DMA leaves the upper audio/control
     bits stale and places 16-bit PCM at the wrong significance. */
  spdif_tx_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
  spdif_tx_dma.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
  spdif_tx_dma.Init.Mode = DMA_CIRCULAR;
  /* The selected output owns the audio path, so give S/PDIF the same
   * deterministic service level previously reserved for SAI1. */
  spdif_tx_dma.Init.Priority = DMA_PRIORITY_VERY_HIGH;
  spdif_tx_dma.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
  spdif_tx_dma.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
  spdif_tx_dma.Init.MemBurst = DMA_MBURST_INC4;
  spdif_tx_dma.Init.PeriphBurst = DMA_PBURST_SINGLE;

  (void)HAL_DMA_DeInit(&spdif_tx_dma);
  hal_status = HAL_DMA_Init(&spdif_tx_dma);
  if (hal_status != HAL_OK)
  {
    SPDIF_TX_DeInit();
    return 0U;
  }
  __HAL_LINKDMA(&spdif_tx_sai, hdmatx, spdif_tx_dma);

  HAL_NVIC_SetPriority(SPDIF_TX_DMA_IRQn, SPDIF_TX_IRQ_PRIORITY, 0U);
  HAL_NVIC_ClearPendingIRQ(SPDIF_TX_DMA_IRQn);
  HAL_NVIC_EnableIRQ(SPDIF_TX_DMA_IRQn);
  spdif_tx_sample_bytes = 4U;
  spdif_tx_initialized = 1U;
  return 1U;
}

void SPDIF_TX_DeInit(void)
{
  uint8_t had_sai = (spdif_tx_sai.Instance == SAI2_Block_A) ? 1U : 0U;
  uint8_t had_dma = (spdif_tx_dma.Instance == SPDIF_TX_DMA_STREAM) ? 1U : 0U;
  SPDIF_TX_Stop();
  HAL_NVIC_DisableIRQ(SPDIF_TX_DMA_IRQn);
  if (had_sai != 0U)
  {
    (void)HAL_SAI_DeInit(&spdif_tx_sai);
  }
  if (had_dma != 0U) (void)HAL_DMA_DeInit(&spdif_tx_dma);
  HAL_GPIO_DeInit(SPDIF_TX_GPIO_Port, SPDIF_TX_Pin);
  __HAL_RCC_SAI2_CLK_DISABLE();
  spdif_tx_initialized = 0U;
  spdif_tx_sample_bytes = 0U;
  spdif_tx_sai.Instance = NULL;
  spdif_tx_dma.Instance = NULL;
}

uint8_t SPDIF_TX_Start(const void *pcm, uint32_t bytes)
{
  uint32_t transfers;
  if ((spdif_tx_initialized == 0U) || (pcm == NULL) ||
      (spdif_tx_sample_bytes == 0U))
  {
    return 0U;
  }
  transfers = bytes / spdif_tx_sample_bytes;
  if ((transfers == 0U) || (transfers > 0xFFFFU))
  {
    return 0U;
  }

  HAL_StatusTypeDef hal_status =
      HAL_SAI_Transmit_DMA(&spdif_tx_sai, (uint8_t *)pcm,
                           (uint16_t)transfers);
  if (hal_status != HAL_OK)
  {
    return 0U;
  }
  /* In mutually-exclusive output mode SAI2 owns the refill cadence. */
  __HAL_DMA_ENABLE_IT(&spdif_tx_dma, DMA_IT_HT | DMA_IT_TC | DMA_IT_TE);
  spdif_tx_running = 1U;
  return 1U;
}

void SPDIF_TX_Stop(void)
{
  if ((spdif_tx_initialized != 0U) ||
      (spdif_tx_sai.Instance == SAI2_Block_A))
  {
    (void)HAL_SAI_DMAStop(&spdif_tx_sai);
  }
  spdif_tx_running = 0U;
}

void SPDIF_TX_Pause(void)
{
  if (spdif_tx_running != 0U) (void)HAL_SAI_DMAPause(&spdif_tx_sai);
}

void SPDIF_TX_Resume(void)
{
  if (spdif_tx_running != 0U) (void)HAL_SAI_DMAResume(&spdif_tx_sai);
}

uint8_t SPDIF_TX_IsRunning(void)
{
  return spdif_tx_running;
}

void SPDIF_TX_DmaIRQHandler(void)
{
  HAL_DMA_IRQHandler(&spdif_tx_dma);
  if (spdif_tx_dma.ErrorCode != HAL_DMA_ERROR_NONE)
  {
    spdif_tx_running = 0U;
  }
}
