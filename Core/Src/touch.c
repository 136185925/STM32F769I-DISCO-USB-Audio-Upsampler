/**
  ******************************************************************************
  * @file    touch.c
  * @brief   Minimal FT6x06 touch controller support for STM32F769I-DISCO.
  ******************************************************************************
  */

#include "touch.h"
#include "i2c.h"
#include "lcd.h"
#include "rtc.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"

#define FT6X06_ADDRESS_REV1       0x54U
#define FT6X06_ADDRESS_A02        0x70U
#define FT6X06_CHIP_ID_REG        0xA8U
#define FT6X06_TD_STATUS_REG      0x02U
#define FT6X06_CHIP_ID            0x11U
#define FT6X06_G_MODE_REG         0xA4U
#define FT6X06_G_MODE_TRIGGER     0x01U
#define FT6X06_G_MODE_MASK        0x03U
#define FT6X06_I2C_TIMEOUT_MS     20U
#define TOUCH_POLL_FALLBACK_MS    5U
#define TOUCH_IDLE_FALLBACK_MS    20U
#define TOUCH_ACTIVE_FALLBACK_MS  10U
#define TOUCH_ASYNC_TIMEOUT_MS    20U

volatile uint8_t touch_chip_id = 0U;
volatile uint8_t touch_i2c_address = 0U;
volatile uint8_t touch_detected = 0U;
volatile uint16_t touch_last_x = 0U;
volatile uint16_t touch_last_y = 0U;
volatile uint32_t touch_i2c_errors = 0U;
volatile uint32_t touch_async_started = 0U;
volatile uint32_t touch_async_completed = 0U;
volatile uint32_t touch_async_timeouts = 0U;
volatile uint32_t touch_async_busy_retries = 0U;
volatile uint32_t touch_irq_count = 0U;
volatile uint32_t touch_fallback_reads = 0U;
volatile uint8_t touch_interrupt_mode = 0U;
volatile uint8_t touch_press_latched = 0U;
volatile uint32_t touch_press_latched_count = 0U;
volatile uint32_t touch_press_consumed_count = 0U;

static uint8_t touch_async_data[5];
static volatile uint8_t touch_async_busy = 0U;
static volatile uint8_t touch_async_ready = 0U;
static volatile uint8_t touch_async_failed = 0U;
static volatile uint8_t touch_async_aborting = 0U;
static volatile uint32_t touch_async_start_tick = 0U;
static volatile uint8_t touch_irq_pending = 1U;
static volatile uint8_t touch_i2c_reserved = 0U;
static TaskHandle_t touch_task_handle = NULL;
static volatile uint16_t touch_latched_x = 0U;
static volatile uint16_t touch_latched_y = 0U;

static HAL_StatusTypeDef Touch_Read(uint8_t reg, uint8_t *data, uint16_t size)
{
  HAL_StatusTypeDef status;

  status = HAL_I2C_Mem_Read(&hi2c4, touch_i2c_address, reg,
                            I2C_MEMADD_SIZE_8BIT, data, size,
                            FT6X06_I2C_TIMEOUT_MS);
  if (status != HAL_OK)
  {
    ++touch_i2c_errors;
  }
  return status;
}

static HAL_StatusTypeDef Touch_Write(uint8_t reg, uint8_t value)
{
  HAL_StatusTypeDef status;

  status = HAL_I2C_Mem_Write(&hi2c4, touch_i2c_address, reg,
                             I2C_MEMADD_SIZE_8BIT, &value, 1U,
                             FT6X06_I2C_TIMEOUT_MS);
  if (status != HAL_OK)
  {
    ++touch_i2c_errors;
  }
  return status;
}

static void Touch_WakeTaskFromISR(void)
{
  BaseType_t higher_priority_task_woken = pdFALSE;

  if (touch_task_handle != NULL)
  {
    vTaskNotifyGiveFromISR(touch_task_handle, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
  }
}

static void Touch_SetReleased(void)
{
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  touch_detected = 0U;
  __DMB();
  if (primask == 0U)
  {
    __enable_irq();
  }
}

static void Touch_ProcessSample(const uint8_t *data)
{
  uint16_t raw_x;
  uint16_t raw_y;
  uint16_t mapped_x;
  uint16_t mapped_y;
  uint16_t delta_x;
  uint16_t delta_y;
  uint8_t detected = data[0] & 0x0FU;
  uint8_t new_contact;
  uint32_t primask;

  if ((detected == 0U) || (detected > 2U))
  {
    Touch_SetReleased();
    return;
  }

  raw_x = (uint16_t)(((uint16_t)(data[1] & 0x0FU) << 8) | data[2]);
  raw_y = (uint16_t)(((uint16_t)(data[3] & 0x0FU) << 8) | data[4]);

  if ((raw_x >= LCD_HEIGHT) || (raw_y >= LCD_WIDTH))
  {
    ++touch_i2c_errors;
    Touch_SetReleased();
    return;
  }

  mapped_x = raw_y;
  mapped_y = (LCD_HEIGHT - 1U) - raw_x;
  delta_x = (mapped_x > touch_last_x) ?
            (mapped_x - touch_last_x) : (touch_last_x - mapped_x);
  delta_y = (mapped_y > touch_last_y) ?
            (mapped_y - touch_last_y) : (touch_last_y - mapped_y);

  primask = __get_PRIMASK();
  __disable_irq();
  new_contact = (touch_detected == 0U) ? 1U : 0U;
  if ((delta_x + delta_y > 3U) || ((touch_last_x == 0U) &&
                                   (touch_last_y == 0U)))
  {
    touch_last_x = mapped_x;
    touch_last_y = mapped_y;
  }
  /* Preserve a complete short tap until TouchGFX has sampled it. Without
     this edge latch, a press and release between two display ticks is lost. */
  if ((new_contact != 0U) && (touch_press_latched == 0U))
  {
    touch_latched_x = mapped_x;
    touch_latched_y = mapped_y;
    __DMB();
    touch_press_latched = 1U;
    ++touch_press_latched_count;
  }
  __DMB();
  touch_detected = detected;
  if (primask == 0U)
  {
    __enable_irq();
  }
}

HAL_StatusTypeDef Touch_Init(void)
{
  static const uint8_t addresses[] = {
    FT6X06_ADDRESS_REV1,
    FT6X06_ADDRESS_A02
  };
  uint32_t index;
  uint8_t id;
  uint8_t mode;

  HAL_Delay(20U);

  for (index = 0U; index < (sizeof(addresses) / sizeof(addresses[0])); ++index)
  {
    if (HAL_I2C_IsDeviceReady(&hi2c4, addresses[index], 3U,
                              FT6X06_I2C_TIMEOUT_MS) != HAL_OK)
    {
      continue;
    }

    touch_i2c_address = addresses[index];
    if ((Touch_Read(FT6X06_CHIP_ID_REG, &id, 1U) == HAL_OK) &&
        (id == FT6X06_CHIP_ID))
    {
      touch_chip_id = id;
      /* ST's 32F769IDISCOVERY BSP uses FT6206 G_MODE trigger mode and
         PI13/TS_INT falling edge. Read back the register so a controller
         variant can automatically fall back to timed polling. */
      if ((Touch_Write(FT6X06_G_MODE_REG, FT6X06_G_MODE_TRIGGER) == HAL_OK) &&
          (Touch_Read(FT6X06_G_MODE_REG, &mode, 1U) == HAL_OK) &&
          ((mode & FT6X06_G_MODE_MASK) == FT6X06_G_MODE_TRIGGER))
      {
        touch_interrupt_mode = 1U;
      }
      return HAL_OK;
    }
  }

  touch_i2c_address = 0U;
  return HAL_ERROR;
}

uint8_t Touch_GetPoint(uint16_t *x, uint16_t *y)
{
  uint8_t detected;
  uint8_t latched;
  uint32_t primask;

  if (touch_i2c_address == 0U)
  {
    return 0U;
  }

  /* The GUI thread only consumes the latest completed sample. This critical
     section is a handful of loads and cannot wait on the I2C peripheral. */
  primask = __get_PRIMASK();
  __disable_irq();
  latched = touch_press_latched;
  if (latched != 0U)
  {
    *x = touch_latched_x;
    *y = touch_latched_y;
    touch_press_latched = 0U;
    ++touch_press_consumed_count;
  }
  else
  {
    *x = touch_last_x;
    *y = touch_last_y;
  }
  detected = touch_detected;
  if (primask == 0U)
  {
    __enable_irq();
  }
  return ((latched != 0U) || (detected != 0U)) ? 1U : 0U;
}

uint8_t Touch_I2C_Reserve(uint32_t timeout_ms)
{
  uint32_t start;

  if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING)
  {
    return 1U;
  }

  taskENTER_CRITICAL();
  if (touch_i2c_reserved != 0U)
  {
    taskEXIT_CRITICAL();
    return 0U;
  }
  touch_i2c_reserved = 1U;
  taskEXIT_CRITICAL();

  if (touch_task_handle != NULL)
  {
    xTaskNotifyGive(touch_task_handle);
  }
  start = HAL_GetTick();
  while (touch_async_busy != 0U)
  {
    if ((HAL_GetTick() - start) >= timeout_ms)
    {
      touch_i2c_reserved = 0U;
      return 0U;
    }
    vTaskDelay(pdMS_TO_TICKS(1U));
  }
  return 1U;
}

void Touch_I2C_Release(void)
{
  touch_i2c_reserved = 0U;
  __DMB();
  if (touch_task_handle != NULL)
  {
    xTaskNotifyGive(touch_task_handle);
  }
}

void Touch_Task(void const *argument)
{
  HAL_StatusTypeDef status;
  uint32_t now;
  uint32_t last_read_tick = 0U;
  uint32_t fallback_period;
  uint32_t wait_ms;
  uint8_t request_from_irq;

  (void)argument;
  touch_task_handle = xTaskGetCurrentTaskHandle();
  touch_irq_pending = 1U;
  for (;;)
  {
    if (touch_async_ready != 0U)
    {
      Touch_ProcessSample(touch_async_data);
      touch_async_ready = 0U;
    }

    if (touch_async_failed != 0U)
    {
      Touch_SetReleased();
      touch_async_failed = 0U;
    }

    now = HAL_GetTick();
    if ((touch_async_busy != 0U) && (touch_async_aborting == 0U) &&
        ((now - touch_async_start_tick) >= TOUCH_ASYNC_TIMEOUT_MS))
    {
      touch_async_aborting = 1U;
      ++touch_async_timeouts;
      ++touch_i2c_errors;
      if (HAL_I2C_Master_Abort_IT(&hi2c4, touch_i2c_address) != HAL_OK)
      {
        touch_async_busy = 0U;
        touch_async_aborting = 0U;
        touch_async_failed = 1U;
      }
    }

    if (touch_interrupt_mode == 0U)
    {
      fallback_period = TOUCH_POLL_FALLBACK_MS;
    }
    else if (touch_detected != 0U)
    {
      fallback_period = TOUCH_ACTIVE_FALLBACK_MS;
    }
    else
    {
      fallback_period = TOUCH_IDLE_FALLBACK_MS;
    }

    request_from_irq = touch_irq_pending;
    taskENTER_CRITICAL();
    status = ((touch_i2c_reserved == 0U) && (touch_i2c_address != 0U) &&
              (touch_async_busy == 0U) && (touch_async_ready == 0U) &&
              (touch_async_aborting == 0U) &&
              ((request_from_irq != 0U) ||
               ((now - last_read_tick) >= fallback_period))) ? HAL_OK : HAL_BUSY;
    if (status == HAL_OK)
    {
      touch_async_busy = 1U;
      touch_async_start_tick = now;
    }
    taskEXIT_CRITICAL();

    if (status == HAL_OK)
    {
      touch_irq_pending = 0U;
      status = HAL_I2C_Mem_Read_IT(&hi2c4, touch_i2c_address,
                                   FT6X06_TD_STATUS_REG,
                                   I2C_MEMADD_SIZE_8BIT,
                                   touch_async_data,
                                   sizeof(touch_async_data));
      if (status == HAL_OK)
      {
        last_read_tick = now;
        ++touch_async_started;
        if (request_from_irq == 0U)
        {
          ++touch_fallback_reads;
        }
      }
      else
      {
        touch_async_busy = 0U;
        ++touch_async_busy_retries;
        if (status != HAL_BUSY)
        {
          ++touch_i2c_errors;
          touch_async_failed = 1U;
        }
      }
    }

    /* I2C completion and PI13 EXTI both wake this task immediately. The
       timeout is only a missed-interrupt/release safety net. */
    now = HAL_GetTick();
    if (touch_async_busy != 0U)
    {
      const uint32_t elapsed = now - touch_async_start_tick;
      wait_ms = (elapsed < TOUCH_ASYNC_TIMEOUT_MS) ?
                (TOUCH_ASYNC_TIMEOUT_MS - elapsed) : 1U;
    }
    else if ((now - last_read_tick) >= fallback_period)
    {
      wait_ms = 1U;
    }
    else
    {
      wait_ms = fallback_period - (now - last_read_tick);
    }
    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_ms));
  }
}

void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c)
{
  if (hi2c == &hi2c4)
  {
    ++touch_async_completed;
    touch_async_busy = 0U;
    touch_async_aborting = 0U;
    __DMB();
    touch_async_ready = 1U;
    Touch_WakeTaskFromISR();
  }
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
  if (hi2c == &hi2c4)
  {
    ++touch_i2c_errors;
    touch_async_ready = 0U;
    touch_async_busy = 0U;
    touch_async_aborting = 0U;
    touch_async_failed = 1U;
    Touch_WakeTaskFromISR();
  }
}

void HAL_I2C_AbortCpltCallback(I2C_HandleTypeDef *hi2c)
{
  if (hi2c == &hi2c4)
  {
    touch_async_ready = 0U;
    touch_async_busy = 0U;
    touch_async_aborting = 0U;
    touch_async_failed = 1U;
    Touch_WakeTaskFromISR();
  }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == B_USER_Pin)
  {
    Stopwatch_UserButtonIRQ();
  }
  else if (GPIO_Pin == LCD_INT_Pin)
  {
    ++touch_irq_count;
    touch_irq_pending = 1U;
    Touch_WakeTaskFromISR();
  }
}
