#ifndef __LCD_H
#define __LCD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f7xx_hal.h"

#define LCD_WIDTH                       800U
#define LCD_HEIGHT                      480U
#define LCD_BYTES_PER_PIXEL             3U

/* The visible image stays 800 pixels wide. A 832-pixel physical pitch makes
   every RGB888 scanline 2496 bytes (39 x 64 bytes), which is friendlier to
   LTDC/DMA2D SDRAM bursts than the old 2400-byte pitch. */
#define LCD_FRAME_BUFFER_STRIDE_PIXELS  832U
#define LCD_FRAME_BUFFER_STRIDE_BYTES   \
  (LCD_FRAME_BUFFER_STRIDE_PIXELS * LCD_BYTES_PER_PIXEL)
#define LCD_FRAME_BUFFER_SIZE           \
  (LCD_FRAME_BUFFER_STRIDE_BYTES * LCD_HEIGHT)

/* The MT48LC4M32B2 SDRAM exposes four 4 MiB banks. Put the two framebuffers
   at the starts of separate banks so LTDC reads and DMA2D writes do not fight
   over the same open SDRAM bank. */
#define LCD_SDRAM_BANK_SIZE             0x00400000UL
#define LCD_FRAME_BUFFER_0              0xC0000000UL
#define LCD_FRAME_BUFFER_1              0xC0400000UL
#define LCD_FRAME_BUFFER                LCD_FRAME_BUFFER_0

#if LCD_FRAME_BUFFER_STRIDE_PIXELS < LCD_WIDTH
#error "Framebuffer stride must cover the visible display width"
#endif
#if (LCD_FRAME_BUFFER_STRIDE_BYTES % 64U) != 0U
#error "RGB888 framebuffer scanlines must remain 64-byte aligned"
#endif
#if LCD_FRAME_BUFFER_SIZE > LCD_SDRAM_BANK_SIZE
#error "A framebuffer must fit inside one 4 MiB SDRAM bank"
#endif

HAL_StatusTypeDef LCD_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* __LCD_H */
