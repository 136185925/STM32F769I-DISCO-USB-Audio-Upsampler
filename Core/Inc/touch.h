#ifndef __TOUCH_H
#define __TOUCH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f7xx_hal.h"

/* These values are intentionally exported to make board bring-up observable
   from a debugger without requiring a UART console. */
extern volatile uint8_t touch_chip_id;
extern volatile uint8_t touch_i2c_address;
extern volatile uint8_t touch_detected;
extern volatile uint16_t touch_last_x;
extern volatile uint16_t touch_last_y;
extern volatile uint32_t touch_i2c_errors;
extern volatile uint32_t touch_async_started;
extern volatile uint32_t touch_async_completed;
extern volatile uint32_t touch_async_timeouts;
extern volatile uint32_t touch_async_busy_retries;
extern volatile uint32_t touch_irq_count;
extern volatile uint32_t touch_fallback_reads;
extern volatile uint8_t touch_interrupt_mode;
extern volatile uint8_t touch_press_latched;
extern volatile uint32_t touch_press_latched_count;
extern volatile uint32_t touch_press_consumed_count;

HAL_StatusTypeDef Touch_Init(void);
uint8_t Touch_GetPoint(uint16_t *x, uint16_t *y);
void Touch_Task(void const *argument);
uint8_t Touch_I2C_Reserve(uint32_t timeout_ms);
void Touch_I2C_Release(void);

#ifdef __cplusplus
}
#endif

#endif /* __TOUCH_H */
