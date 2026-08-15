/**
  ******************************************************************************
  * @file    lcd.c
  * @brief   Minimal DSI LCD support for the STM32F769I-DISCO.
  *
  * The panel setup uses ST's OTM8009A component driver. Pixels are stored as
  * The RGB888 framebuffer in external SDRAM is owned by TouchGFX.
  ******************************************************************************
  */

#include "lcd.h"
#include "dsihost.h"
#include "main.h"
#include "otm8009a.h"

#include <stddef.h>

#define RGB565(r, g, b) ((uint16_t)((((uint16_t)(r) & 0xF8U) << 8) | \
                                    (((uint16_t)(g) & 0xFCU) << 3) | \
                                    (((uint16_t)(b)) >> 3)))

#define TOUCH_SQUARE_SIZE  32U

static OTM8009A_Object_t lcd_panel;
static volatile uint16_t *const frame_buffer =
    (volatile uint16_t *)LCD_FRAME_BUFFER;
static volatile uint8_t lcd_refresh_busy = 0U;
static uint8_t touch_square_visible = 0U;
static uint16_t touch_square_x = 0U;
static uint16_t touch_square_y = 0U;
static uint16_t touch_square_background[TOUCH_SQUARE_SIZE * TOUCH_SQUARE_SIZE];

static int32_t LCD_DSI_WriteReg(uint16_t address, uint16_t reg,
                                uint8_t *data, uint16_t length);
static int32_t LCD_DSI_ReadReg(uint16_t address, uint16_t reg,
                               uint8_t *data, uint16_t length);
static int32_t LCD_GetTick(void);
static void LCD_Reset(void);
static void LCD_Fill(uint16_t color);
static void LCD_FillRect(uint32_t x, uint32_t y, uint32_t width,
                         uint32_t height, uint16_t color);
static const uint8_t *LCD_Glyph(char ch);
static void LCD_DrawChar(uint32_t x, uint32_t y, char ch, uint32_t scale,
                         uint16_t color);
static void LCD_DrawTextCentered(const char *text, uint32_t y, uint32_t scale,
                                 uint16_t color);
static HAL_StatusTypeDef LCD_Refresh(void);
static void LCD_SaveTouchBackground(uint16_t x, uint16_t y);
static void LCD_RestoreTouchBackground(void);

HAL_StatusTypeDef LCD_Init(void)
{
  OTM8009A_IO_t io = {0};

  LCD_Reset();

  if (HAL_DSI_Start(&hdsi) != HAL_OK)
  {
    return HAL_ERROR;
  }

  io.Address = 0U;
  io.WriteReg = LCD_DSI_WriteReg;
  io.ReadReg = LCD_DSI_ReadReg;
  io.GetTick = LCD_GetTick;

  if (OTM8009A_RegisterBusIO(&lcd_panel, &io) != OTM8009A_OK)
  {
    return HAL_ERROR;
  }

  if (OTM8009A_Init(&lcd_panel, OTM8009A_FORMAT_RGB888,
                    OTM8009A_ORIENTATION_LANDSCAPE) != OTM8009A_OK)
  {
    return HAL_ERROR;
  }

  /* In Video Burst mode LTDC continuously streams the framebuffer. DCS
     control packets remain enabled in LP blanking intervals; there is no
     per-frame HAL_DSI_Refresh() transaction to configure or wait for. */
  lcd_refresh_busy = 0U;
  return HAL_OK;
}

void LCD_DrawHelloWorld(void)
{
  uint32_t y;

  LCD_Fill(RGB565(9, 22, 42));

  /* A two-tone background makes a successful RGB transfer obvious. */
  for (y = 0U; y < 110U; ++y)
  {
    uint8_t green = (uint8_t)(70U + (y / 3U));
    LCD_FillRect(0U, y, LCD_WIDTH, 1U, RGB565(16, green, 145));
  }

  LCD_FillRect(92U, 163U, 616U, 154U, RGB565(18, 39, 68));
  LCD_FillRect(92U, 163U, 616U, 4U, RGB565(42, 204, 210));
  LCD_FillRect(92U, 313U, 616U, 4U, RGB565(42, 204, 210));

  LCD_DrawTextCentered("Hello World", 211U, 8U, RGB565(3, 13, 25));
  LCD_DrawTextCentered("Hello World", 205U, 8U, RGB565(245, 249, 255));
}

uint8_t LCD_IsRefreshReady(void)
{
  return (uint8_t)(lcd_refresh_busy == 0U);
}

HAL_StatusTypeDef LCD_SetTouchSquare(uint16_t x, uint16_t y, uint8_t visible)
{
  uint16_t new_x;
  uint16_t new_y;
  uint8_t restored = 0U;

  if (lcd_refresh_busy != 0U)
  {
    return HAL_BUSY;
  }

  new_x = (x < (TOUCH_SQUARE_SIZE / 2U)) ?
          0U : (uint16_t)(x - (TOUCH_SQUARE_SIZE / 2U));
  new_y = (y < (TOUCH_SQUARE_SIZE / 2U)) ?
          0U : (uint16_t)(y - (TOUCH_SQUARE_SIZE / 2U));
  if (new_x > (LCD_WIDTH - TOUCH_SQUARE_SIZE))
  {
    new_x = LCD_WIDTH - TOUCH_SQUARE_SIZE;
  }
  if (new_y > (LCD_HEIGHT - TOUCH_SQUARE_SIZE))
  {
    new_y = LCD_HEIGHT - TOUCH_SQUARE_SIZE;
  }

  if (touch_square_visible != 0U)
  {
    if ((visible != 0U) &&
        (new_x == touch_square_x) && (new_y == touch_square_y))
    {
      return HAL_OK;
    }
    LCD_RestoreTouchBackground();
    touch_square_visible = 0U;
    restored = 1U;
  }

  if (visible == 0U)
  {
    return (restored != 0U) ? LCD_Refresh() : HAL_OK;
  }

  touch_square_x = new_x;
  touch_square_y = new_y;
  LCD_SaveTouchBackground(new_x, new_y);
  LCD_FillRect(new_x, new_y, TOUCH_SQUARE_SIZE, TOUCH_SQUARE_SIZE,
               RGB565(255, 184, 36));
  LCD_FillRect(new_x + 4U, new_y + 4U,
               TOUCH_SQUARE_SIZE - 8U, TOUCH_SQUARE_SIZE - 8U,
               RGB565(255, 246, 216));
  touch_square_visible = 1U;

  return LCD_Refresh();
}

static HAL_StatusTypeDef LCD_Refresh(void)
{
  /* Video mode is continuously refreshed by LTDC/DSI. */
  return HAL_OK;
}

static void LCD_SaveTouchBackground(uint16_t x, uint16_t y)
{
  uint32_t row;
  uint32_t column;

  for (row = 0U; row < TOUCH_SQUARE_SIZE; ++row)
  {
    for (column = 0U; column < TOUCH_SQUARE_SIZE; ++column)
    {
      touch_square_background[row * TOUCH_SQUARE_SIZE + column] =
          frame_buffer[(y + row) * LCD_WIDTH + x + column];
    }
  }
}

static void LCD_RestoreTouchBackground(void)
{
  uint32_t row;
  uint32_t column;

  for (row = 0U; row < TOUCH_SQUARE_SIZE; ++row)
  {
    for (column = 0U; column < TOUCH_SQUARE_SIZE; ++column)
    {
      frame_buffer[(touch_square_y + row) * LCD_WIDTH +
                   touch_square_x + column] =
          touch_square_background[row * TOUCH_SQUARE_SIZE + column];
    }
  }
}

static int32_t LCD_DSI_WriteReg(uint16_t address, uint16_t reg,
                                uint8_t *data, uint16_t length)
{
  HAL_StatusTypeDef status;

  if (length <= 1U)
  {
    status = HAL_DSI_ShortWrite(&hdsi, address, DSI_DCS_SHORT_PKT_WRITE_P1,
                                reg, data[0]);
  }
  else
  {
    status = HAL_DSI_LongWrite(&hdsi, address, DSI_DCS_LONG_PKT_WRITE,
                               length, reg, data);
  }

  return (status == HAL_OK) ? OTM8009A_OK : OTM8009A_ERROR;
}

static int32_t LCD_DSI_ReadReg(uint16_t address, uint16_t reg,
                               uint8_t *data, uint16_t length)
{
  HAL_StatusTypeDef status;

  status = HAL_DSI_Read(&hdsi, address, data, length, DSI_DCS_SHORT_PKT_READ,
                        reg, data);
  return (status == HAL_OK) ? OTM8009A_OK : OTM8009A_ERROR;
}

static int32_t LCD_GetTick(void)
{
  return (int32_t)HAL_GetTick();
}

static void LCD_Reset(void)
{
  HAL_GPIO_WritePin(DSI_RESET_GPIO_Port, DSI_RESET_Pin, GPIO_PIN_RESET);
  HAL_Delay(20U);
  HAL_GPIO_WritePin(DSI_RESET_GPIO_Port, DSI_RESET_Pin, GPIO_PIN_SET);
  HAL_Delay(10U);
}

static void LCD_Fill(uint16_t color)
{
  uint32_t index;

  for (index = 0U; index < (LCD_WIDTH * LCD_HEIGHT); ++index)
  {
    frame_buffer[index] = color;
  }
}

static void LCD_FillRect(uint32_t x, uint32_t y, uint32_t width,
                         uint32_t height, uint16_t color)
{
  uint32_t row;
  uint32_t column;

  if ((x >= LCD_WIDTH) || (y >= LCD_HEIGHT))
  {
    return;
  }

  if ((x + width) > LCD_WIDTH)
  {
    width = LCD_WIDTH - x;
  }
  if ((y + height) > LCD_HEIGHT)
  {
    height = LCD_HEIGHT - y;
  }

  for (row = 0U; row < height; ++row)
  {
    volatile uint16_t *pixel = &frame_buffer[(y + row) * LCD_WIDTH + x];
    for (column = 0U; column < width; ++column)
    {
      pixel[column] = color;
    }
  }
}

static const uint8_t *LCD_Glyph(char ch)
{
  static const uint8_t blank[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
  static const uint8_t upper_h[5] = {0x7F, 0x08, 0x08, 0x08, 0x7F};
  static const uint8_t lower_e[5] = {0x38, 0x54, 0x54, 0x54, 0x18};
  static const uint8_t lower_l[5] = {0x00, 0x41, 0x7F, 0x40, 0x00};
  static const uint8_t lower_o[5] = {0x38, 0x44, 0x44, 0x44, 0x38};
  static const uint8_t upper_w[5] = {0x3F, 0x40, 0x38, 0x40, 0x3F};
  static const uint8_t lower_r[5] = {0x7C, 0x08, 0x04, 0x04, 0x08};
  static const uint8_t lower_d[5] = {0x38, 0x44, 0x44, 0x48, 0x7F};

  switch (ch)
  {
    case 'H': return upper_h;
    case 'e': return lower_e;
    case 'l': return lower_l;
    case 'o': return lower_o;
    case 'W': return upper_w;
    case 'r': return lower_r;
    case 'd': return lower_d;
    default:  return blank;
  }
}

static void LCD_DrawChar(uint32_t x, uint32_t y, char ch, uint32_t scale,
                         uint16_t color)
{
  const uint8_t *glyph = LCD_Glyph(ch);
  uint32_t column;
  uint32_t row;

  for (column = 0U; column < 5U; ++column)
  {
    for (row = 0U; row < 7U; ++row)
    {
      if ((glyph[column] & (1U << row)) != 0U)
      {
        LCD_FillRect(x + column * scale, y + row * scale, scale, scale, color);
      }
    }
  }
}

static void LCD_DrawTextCentered(const char *text, uint32_t y, uint32_t scale,
                                 uint16_t color)
{
  size_t length = 0U;
  uint32_t x;

  while (text[length] != '\0')
  {
    ++length;
  }

  x = (LCD_WIDTH - ((uint32_t)length * 6U * scale)) / 2U;
  while (*text != '\0')
  {
    LCD_DrawChar(x, y, *text, scale, color);
    x += 6U * scale;
    ++text;
  }
}
