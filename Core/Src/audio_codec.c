#include "audio_codec.h"

#include "i2c.h"
#include "stm32f769i_discovery_audio.h"
#include "FreeRTOS.h"
#include "task.h"

#define WM8994_ADDRESS 0x34U
#define CODEC_I2C_TIMEOUT_MS 50U

volatile uint16_t audio_codec_id = 0U;
volatile uint8_t audio_codec_ready = 0U;
volatile uint32_t audio_codec_i2c_errors = 0U;

void AUDIO_IO_Init(void)
{
  if (hi2c4.State == HAL_I2C_STATE_RESET)
  {
    MX_I2C4_Init();
  }
}

void AUDIO_IO_DeInit(void)
{
  /* I2C4 is shared with the FT6206 touch controller and remains enabled. */
}

void AUDIO_IO_Write(uint8_t address, uint16_t reg, uint16_t value)
{
  uint8_t data[2];
  data[0] = (uint8_t)(value >> 8);
  data[1] = (uint8_t)value;
  if (HAL_I2C_Mem_Write(&hi2c4, address, reg, I2C_MEMADD_SIZE_16BIT,
                        data, sizeof(data), CODEC_I2C_TIMEOUT_MS) != HAL_OK)
  {
    ++audio_codec_i2c_errors;
  }
}

uint16_t AUDIO_IO_Read(uint8_t address, uint16_t reg)
{
  uint8_t data[2] = {0U, 0U};
  if (HAL_I2C_Mem_Read(&hi2c4, address, reg, I2C_MEMADD_SIZE_16BIT,
                       data, sizeof(data), CODEC_I2C_TIMEOUT_MS) != HAL_OK)
  {
    ++audio_codec_i2c_errors;
    return 0U;
  }
  return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

void AUDIO_IO_Delay(uint32_t delay_ms)
{
  if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
  {
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
  }
  else
  {
    HAL_Delay(delay_ms);
  }
}

void AudioCodec_BootInit(void)
{
  audio_codec_ready = 0U;
  audio_codec_id = (uint16_t)wm8994_drv.ReadID(WM8994_ADDRESS);
  if (audio_codec_id != WM8994_ID)
  {
    return;
  }

  if ((wm8994_drv.Reset(WM8994_ADDRESS) == 0U) &&
      (wm8994_drv.Init(WM8994_ADDRESS, OUTPUT_DEVICE_HEADPHONE,
                       0U, AUDIO_FREQUENCY_16K) == 0U) &&
      (wm8994_drv.SetMute(WM8994_ADDRESS, AUDIO_MUTE_ON) == 0U))
  {
    audio_codec_ready = 1U;
  }
}
