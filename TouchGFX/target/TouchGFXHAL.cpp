/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : TouchGFXHAL.cpp
  ******************************************************************************
  * This file was created by TouchGFX Generator 4.26.1. This file is only
  * generated once! Delete this file from your project and re-generate code
  * using STM32CubeMX or change this file manually to update it.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

#include <TouchGFXHAL.hpp>

#include <touchgfx/hal/HAL.hpp>
#include <touchgfx/hal/OSWrappers.hpp>

extern "C"
{
#include "dsihost.h"
#include "dma2d.h"
#include "lcd.h"
#include "ltdc.h"
}

/* USER CODE BEGIN TouchGFXHAL.cpp */

using namespace touchgfx;

static volatile bool refreshRequested = false;
static volatile uint32_t activeFrameBuffer = LCD_FRAME_BUFFER_0;
static volatile bool backBufferNeedsSync = false;

extern "C"
{
extern volatile uint32_t touchgfx_framebuffer_error_count;
extern volatile uint32_t touchgfx_dirty_sync_count;
}

namespace
{
const uint8_t MAX_DIRTY_REGIONS = 8U;

struct DirtyRegion
{
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
};

DirtyRegion pendingDirtyRegions[MAX_DIRTY_REGIONS];
DirtyRegion currentDirtyRegions[MAX_DIRTY_REGIONS];
uint8_t pendingDirtyRegionCount = 0U;
uint8_t currentDirtyRegionCount = 0U;

bool clipToDisplay(const touchgfx::Rect& rect, DirtyRegion& clipped)
{
    int32_t left = rect.x;
    int32_t top = rect.y;
    int32_t right = static_cast<int32_t>(rect.x) + rect.width;
    int32_t bottom = static_cast<int32_t>(rect.y) + rect.height;

    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > static_cast<int32_t>(LCD_WIDTH)) right = LCD_WIDTH;
    if (bottom > static_cast<int32_t>(LCD_HEIGHT)) bottom = LCD_HEIGHT;
    if ((right <= left) || (bottom <= top))
    {
        return false;
    }

    clipped.x = static_cast<uint16_t>(left);
    clipped.y = static_cast<uint16_t>(top);
    clipped.width = static_cast<uint16_t>(right - left);
    clipped.height = static_cast<uint16_t>(bottom - top);
    return true;
}

bool regionsTouch(const DirtyRegion& a, const DirtyRegion& b)
{
    const uint32_t aRight = static_cast<uint32_t>(a.x) + a.width;
    const uint32_t aBottom = static_cast<uint32_t>(a.y) + a.height;
    const uint32_t bRight = static_cast<uint32_t>(b.x) + b.width;
    const uint32_t bBottom = static_cast<uint32_t>(b.y) + b.height;
    return (a.x <= bRight) && (b.x <= aRight) &&
           (a.y <= bBottom) && (b.y <= aBottom);
}

DirtyRegion unionRegion(const DirtyRegion& a, const DirtyRegion& b)
{
    const uint16_t left = (a.x < b.x) ? a.x : b.x;
    const uint16_t top = (a.y < b.y) ? a.y : b.y;
    const uint32_t aRight = static_cast<uint32_t>(a.x) + a.width;
    const uint32_t bRight = static_cast<uint32_t>(b.x) + b.width;
    const uint32_t aBottom = static_cast<uint32_t>(a.y) + a.height;
    const uint32_t bBottom = static_cast<uint32_t>(b.y) + b.height;
    const uint32_t right = (aRight > bRight) ? aRight : bRight;
    const uint32_t bottom = (aBottom > bBottom) ? aBottom : bBottom;
    DirtyRegion result = {
        left,
        top,
        static_cast<uint16_t>(right - left),
        static_cast<uint16_t>(bottom - top)
    };
    return result;
}

void addDirtyRegion(DirtyRegion regions[MAX_DIRTY_REGIONS],
                    uint8_t& count,
                    DirtyRegion added)
{
    /* Merge overlapping/adjacent invalidations. If the fixed list fills up,
       collapse it to one bounding box rather than allocating GUI heap memory. */
    uint8_t index = 0U;
    while (index < count)
    {
        if (regionsTouch(regions[index], added))
        {
            added = unionRegion(regions[index], added);
            regions[index] = regions[count - 1U];
            --count;
            index = 0U;
        }
        else
        {
            ++index;
        }
    }

    if (count < MAX_DIRTY_REGIONS)
    {
        regions[count++] = added;
        return;
    }

    for (index = 0U; index < count; ++index)
    {
        added = unionRegion(regions[index], added);
    }
    regions[0U] = added;
    count = 1U;
}

bool copyDirtyRegion(uint32_t front, uint32_t back, const DirtyRegion& region)
{
    if ((region.width == 0U) || (region.height == 0U))
    {
        return true;
    }
    if (TouchGFX_DMA2D_CopyRGB888Rect(front, back,
                                     region.x, region.y,
                                     region.width, region.height,
                                     LCD_FRAME_BUFFER_STRIDE_PIXELS) != HAL_OK)
    {
        ++touchgfx_framebuffer_error_count;
        return false;
    }
    return true;
}

void synchronizeBackBuffer(const DirtyRegion* preserve)
{
    if (!HAL::USE_DOUBLE_BUFFERING || !backBufferNeedsSync)
    {
        return;
    }

    const uint32_t front = activeFrameBuffer;
    const uint32_t back = (front == LCD_FRAME_BUFFER_0) ?
        LCD_FRAME_BUFFER_1 : LCD_FRAME_BUFFER_0;

    for (uint8_t index = 0U; index < pendingDirtyRegionCount; ++index)
    {
        const DirtyRegion& source = pendingDirtyRegions[index];
        if (preserve == 0)
        {
            (void)copyDirtyRegion(front, back, source);
            continue;
        }

        const uint32_t sourceRight = static_cast<uint32_t>(source.x) + source.width;
        const uint32_t sourceBottom = static_cast<uint32_t>(source.y) + source.height;
        const uint32_t preserveRight = static_cast<uint32_t>(preserve->x) + preserve->width;
        const uint32_t preserveBottom = static_cast<uint32_t>(preserve->y) + preserve->height;
        const uint16_t intersectLeft = (source.x > preserve->x) ? source.x : preserve->x;
        const uint16_t intersectTop = (source.y > preserve->y) ? source.y : preserve->y;
        const uint32_t intersectRight = (sourceRight < preserveRight) ? sourceRight : preserveRight;
        const uint32_t intersectBottom = (sourceBottom < preserveBottom) ? sourceBottom : preserveBottom;

        if ((intersectRight <= intersectLeft) || (intersectBottom <= intersectTop))
        {
            (void)copyDirtyRegion(front, back, source);
            continue;
        }

        /* flushFrameBuffer() is reached after its rectangle was rendered. If
           no earlier DMA2D/block-copy hook synchronized the buffers, copy the
           old dirty area around (but never over) those new pixels. */
        const DirtyRegion pieces[4] = {
            {source.x, source.y, source.width,
             static_cast<uint16_t>(intersectTop - source.y)},
            {source.x, static_cast<uint16_t>(intersectBottom), source.width,
             static_cast<uint16_t>(sourceBottom - intersectBottom)},
            {source.x, intersectTop,
             static_cast<uint16_t>(intersectLeft - source.x),
             static_cast<uint16_t>(intersectBottom - intersectTop)},
            {static_cast<uint16_t>(intersectRight), intersectTop,
             static_cast<uint16_t>(sourceRight - intersectRight),
             static_cast<uint16_t>(intersectBottom - intersectTop)}
        };
        for (uint8_t piece = 0U; piece < 4U; ++piece)
        {
            (void)copyDirtyRegion(front, back, pieces[piece]);
        }
    }

    pendingDirtyRegionCount = 0U;
    backBufferNeedsSync = false;
    ++touchgfx_dirty_sync_count;
}
}

extern "C"
{
volatile uint32_t touchgfx_vsync_count = 0U;
volatile uint32_t touchgfx_refresh_count = 0U;
volatile uint32_t touchgfx_render_wait_count = 0U;
volatile uint32_t touchgfx_refresh_recovery_count = 0U;
volatile uint32_t touchgfx_framebuffer_swap_count = 0U;
volatile uint32_t touchgfx_framebuffer_error_count = 0U;
volatile uint32_t touchgfx_active_framebuffer = LCD_FRAME_BUFFER_0;
volatile uint32_t touchgfx_ltdc_error_count = 0U;
volatile uint32_t touchgfx_ltdc_last_error = HAL_LTDC_ERROR_NONE;
volatile uint32_t touchgfx_full_redraw_skip_count = 0U;
volatile uint32_t touchgfx_dirty_sync_count = 0U;
}

void TouchGFXHAL::initialize()
{
    // Calling parent implementation of initialize().
    //
    // To overwrite the generated implementation, omit the call to the parent function
    // and implement the needed functionality here.
    // Please note, HAL::initialize() must be called to initialize the framework.

    TouchGFXGeneratedHAL::initialize();
    /* Logical rendering remains 800x480; only the physical RGB888 row pitch
       includes the 32 padding pixels configured in LTDC. */
    setFrameBufferSize(LCD_FRAME_BUFFER_STRIDE_PIXELS, LCD_HEIGHT);
    /* TouchGFX renders into the buffer opposite the one continuously scanned
       by LTDC. Buffer swaps are committed only in the LTDC VSYNC callback. */
    lockDMAToFrontPorch(false);
}

/**
 * Gets the frame buffer address used by the TFT controller.
 *
 * @return The address of the frame buffer currently being displayed on the TFT.
 */
uint16_t* TouchGFXHAL::getTFTFrameBuffer() const
{
    // Calling parent implementation of getTFTFrameBuffer().
    //
    // To overwrite the generated implementation, omit the call to the parent function
    // and implement the needed functionality here.

    return reinterpret_cast<uint16_t*>(activeFrameBuffer);
}

/**
 * Sets the frame buffer address used by the TFT controller.
 *
 * @param [in] address New frame buffer address.
 */
void TouchGFXHAL::setTFTFrameBuffer(uint16_t* address)
{
    // Calling parent implementation of setTFTFrameBuffer(uint16_t* address).
    //
    // To overwrite the generated implementation, omit the call to the parent function
    // and implement the needed functionality here.

    const uint32_t next = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(address));
    if ((next != LCD_FRAME_BUFFER_0) && (next != LCD_FRAME_BUFFER_1))
    {
        ++touchgfx_framebuffer_error_count;
        return;
    }

    /* Called from the line-0 (vertical blanking) interrupt. This follows ST's
       VideoMode_DoubleBuffering example: update CFBAR and reload while the
       active image has not started, so one complete buffer is scanned. */
    LTDC_LAYER(&hltdc, 0U)->CFBAR = next;
    hltdc.LayerCfg[0U].FBStartAdress = next;
    __HAL_LTDC_RELOAD_CONFIG(&hltdc);
    __DSB();
    activeFrameBuffer = next;
    touchgfx_active_framebuffer = next;
    ++touchgfx_framebuffer_swap_count;
}

/**
 * This function is called whenever the framework has performed a partial draw.
 *
 * @param rect The area of the screen that has been drawn, expressed in absolute coordinates.
 *
 * @see flushFrameBuffer().
 */
void TouchGFXHAL::flushFrameBuffer(const touchgfx::Rect& rect)
{
    // Calling parent implementation of flushFrameBuffer(const touchgfx::Rect& rect).
    //
    // To overwrite the generated implementation, omit the call to the parent function
    // and implement the needed functionality here.
    // Please note, HAL::flushFrameBuffer(const touchgfx::Rect& rect) must
    // be called to notify the touchgfx framework that flush has been performed.
    // To calculate the start address of rect,
    // use advanceFrameBufferToRect(uint8_t* fbPtr, const touchgfx::Rect& rect)
    // defined in TouchGFXGeneratedHAL.cpp

    DirtyRegion updated;
    if (clipToDisplay(rect, updated))
    {
        synchronizeBackBuffer(&updated);
        addDirtyRegion(currentDirtyRegions, currentDirtyRegionCount, updated);
    }
    TouchGFXGeneratedHAL::flushFrameBuffer(rect);
}

bool TouchGFXHAL::blockCopy(void* RESTRICT dest, const void* RESTRICT src, uint32_t numBytes)
{
    TouchGFX_WaitForDisplayRefresh();
    return TouchGFXGeneratedHAL::blockCopy(dest, src, numBytes);
}

/**
 * Configures the interrupts relevant for TouchGFX. This primarily entails setting
 * the interrupt priorities for the DMA and LCD interrupts.
 */
void TouchGFXHAL::configureInterrupts()
{
    // Calling parent implementation of configureInterrupts().
    //
    // To overwrite the generated implementation, omit the call to the parent function
    // and implement the needed functionality here.

    HAL_NVIC_SetPriority(LTDC_IRQn, 7U, 0U);
}

/**
 * Used for enabling interrupts set in configureInterrupts()
 */
void TouchGFXHAL::enableInterrupts()
{
    // Calling parent implementation of enableInterrupts().
    //
    // To overwrite the generated implementation, omit the call to the parent function
    // and implement the needed functionality here.

    HAL_NVIC_EnableIRQ(LTDC_IRQn);
}

/**
 * Used for disabling interrupts set in configureInterrupts()
 */
void TouchGFXHAL::disableInterrupts()
{
    // Calling parent implementation of disableInterrupts().
    //
    // To overwrite the generated implementation, omit the call to the parent function
    // and implement the needed functionality here.

    HAL_NVIC_DisableIRQ(LTDC_IRQn);
}

/**
 * Configure the LCD controller to fire interrupts at VSYNC. Called automatically
 * once TouchGFX initialization has completed.
 */
void TouchGFXHAL::enableLCDControllerInterrupt()
{
    // Calling parent implementation of enableLCDControllerInterrupt().
    //
    // To overwrite the generated implementation, omit the call to the parent function
    // and implement the needed functionality here.

    /* HAL_LTDC_Init starts the pixel generator before the DSI panel and
       TouchGFX are ready, which may produce one invisible startup underrun.
       Establish the runtime diagnostic baseline only after the full display
       pipeline has been initialized; later underruns remain reported. */
    __HAL_LTDC_CLEAR_FLAG(&hltdc, LTDC_FLAG_LI | LTDC_FLAG_FU | LTDC_FLAG_TE);
    hltdc.ErrorCode = HAL_LTDC_ERROR_NONE;
    touchgfx_ltdc_last_error = HAL_LTDC_ERROR_NONE;
    touchgfx_ltdc_error_count = 0U;
    (void)HAL_LTDC_ProgramLineEvent(&hltdc, 0U);
}

bool TouchGFXHAL::beginFrame()
{
    /* When both buffers are occupied, TouchGFX returns false until the next
       VSYNC callback consumes the pending swap. Do not discard that request. */
    const bool canRender = TouchGFXGeneratedHAL::beginFrame();
    if (canRender)
    {
        refreshRequested = false;
        currentDirtyRegionCount = 0U;
        backBufferNeedsSync = (pendingDirtyRegionCount != 0U);
    }
    return canRender;
}

void TouchGFXHAL::endFrame()
{
    TouchGFXGeneratedHAL::endFrame();
    if (frameBufferUpdatedThisFrame)
    {
        if (currentDirtyRegionCount == 0U)
        {
            const DirtyRegion fullDisplay = {0U, 0U, LCD_WIDTH, LCD_HEIGHT};
            addDirtyRegion(currentDirtyRegions, currentDirtyRegionCount, fullDisplay);
        }
        pendingDirtyRegionCount = currentDirtyRegionCount;
        for (uint8_t index = 0U; index < currentDirtyRegionCount; ++index)
        {
            pendingDirtyRegions[index] = currentDirtyRegions[index];
        }
        backBufferNeedsSync = false;
        refreshRequested = true;
    }
}

extern "C" void HAL_LTDC_LineEventCallback(LTDC_HandleTypeDef* ltdc)
{
    if (ltdc != &hltdc)
    {
        return;
    }

    const bool pendingRefresh = refreshRequested;
    ++touchgfx_vsync_count;
    if (HAL::getInstance() != 0)
    {
        HAL::getInstance()->vSync();
        HAL::getInstance()->lockDMAToFrontPorch(false);
    }

    if (pendingRefresh)
    {
        /* Commit the completed back buffer at the beginning of vertical
           blanking. DSI Video Burst then scans it continuously for one frame. */
        if (HAL::getInstance() != 0)
        {
            HAL::getInstance()->swapFrameBuffers();
            HAL::getInstance()->frontPorchEntered();
        }
        refreshRequested = false;
        ++touchgfx_refresh_count;
    }
    OSWrappers::signalVSync();
    (void)HAL_LTDC_ProgramLineEvent(ltdc, 0U);
}

extern "C" void TouchGFX_WaitForDisplayRefresh(void)
{
    /* Restore only pixels changed in the previously displayed frame. The
       current back buffer already contains every other pixel. */
    synchronizeBackBuffer(0);
}

extern "C" void TouchGFX_BeginFullScreenRedraw(void)
{
    /* A widget that guarantees complete 800x480 coverage does not need the
       previous front buffer as a starting point. Skipping this RGB888 copy
       removes 1.15 MB of reads and 1.15 MB of writes from every motion frame. */
    if (HAL::USE_DOUBLE_BUFFERING && backBufferNeedsSync)
    {
        pendingDirtyRegionCount = 0U;
        backBufferNeedsSync = false;
        ++touchgfx_full_redraw_skip_count;
    }
}

extern "C" uint32_t TouchGFX_GetClientFrameBuffer(void)
{
    return (activeFrameBuffer == LCD_FRAME_BUFFER_0) ?
        LCD_FRAME_BUFFER_1 : LCD_FRAME_BUFFER_0;
}

extern "C" void HAL_LTDC_ErrorCallback(LTDC_HandleTypeDef* ltdc)
{
    if (ltdc == &hltdc)
    {
        touchgfx_ltdc_last_error = ltdc->ErrorCode;
        ++touchgfx_ltdc_error_count;
    }
}

/* USER CODE END TouchGFXHAL.cpp */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
