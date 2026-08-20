#include <gui/main_screen/MainView.hpp>
#include <touchgfx/hal/HAL.hpp>
#include <touchgfx/lcd/LCD.hpp>
#include <cstring>

extern "C" {
#include "dma2d.h"
#include "lcd.h"
#include "fatfs.h"
#include "audio_recorder.h"
#include "audio_player.h"
#include "audio_spectrum.h"
#include "rtc.h"
#include "usb_msc.h"
#include "usb_audio.h"
#include "FreeRTOS.h"
extern uint8_t _sidata;
extern uint8_t _sdata;
extern uint8_t _edata;
extern uint8_t _end;
extern uint8_t _Min_Heap_Size;
extern uint8_t _Min_Stack_Size;
}

using namespace touchgfx;

extern "C" {
volatile uint32_t desktop_swipe_count = 0;
volatile uint32_t desktop_app_open_count = 0;
volatile uint32_t desktop_app_close_count = 0;
volatile uint8_t desktop_current_page = 0;
volatile uint8_t desktop_current_app = 0xFF;
}

namespace
{
const char* const APP_NAMES[16] = {
    "PHOTOS", "MUSIC", "FILES", "NOTES",
    "STOPWATCH", "WEATHER", "CALCULATOR", "MAPS",
    "CAMERA", "CALENDAR", "SPECTRUM", "SETTINGS",
    "BOOKS", "RECORDER", "VIDEOS", "SYSTEM"
};

const uint8_t APP_COLORS[16][3] = {
    {248, 105, 124}, {157, 92, 240}, {48, 156, 246}, {245, 194, 66},
    {72, 84, 105}, {42, 177, 232}, {242, 151, 45}, {77, 190, 139},
    {81, 91, 111}, {244, 83, 82}, {48, 190, 145}, {126, 137, 153},
    {235, 139, 62}, {251, 178, 59}, {229, 72, 105}, {66, 166, 246}
};

const int16_t ICON_X[4] = {70, 250, 430, 610};
const int16_t ICON_Y[2] = {112, 272};
const int16_t SETTINGS_VIEW_TOP = 112;
const int16_t SETTINGS_VIEW_BOTTOM = 447;
const int16_t SETTINGS_CONTENT_BOTTOM = 721;
const int16_t SETTINGS_SCROLL_MAX =
    SETTINGS_CONTENT_BOTTOM - SETTINGS_VIEW_BOTTOM;
const int16_t SETTINGS_BUFFER_SLIDER_X = 220;
const int16_t SETTINGS_BUFFER_SLIDER_WIDTH = 430;

colortype rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return Color::getColorFromRGB(r, g, b);
}

uint8_t settingsGainFromX(int16_t x)
{
    if (x <= 220) return 0U;
    if (x >= 610) return 100U;
    return static_cast<uint8_t>(
        ((static_cast<uint32_t>(x - 220) * 100U) + 195U) / 390U);
}

uint32_t settingsStartFramesFromX(int16_t x)
{
    if (x <= SETTINGS_BUFFER_SLIDER_X) return USB_AUDIO_START_FRAMES_MIN;
    if (x >= SETTINGS_BUFFER_SLIDER_X + SETTINGS_BUFFER_SLIDER_WIDTH) {
        return USB_AUDIO_START_FRAMES_MAX;
    }
    const uint32_t stepCount =
        (USB_AUDIO_START_FRAMES_MAX - USB_AUDIO_START_FRAMES_MIN) /
        USB_AUDIO_START_FRAMES_STEP;
    const uint32_t selectedStep =
        (static_cast<uint32_t>(x - SETTINGS_BUFFER_SLIDER_X) * stepCount +
         SETTINGS_BUFFER_SLIDER_WIDTH / 2U) /
        SETTINGS_BUFFER_SLIDER_WIDTH;
    return USB_AUDIO_START_FRAMES_MIN +
           selectedStep * USB_AUDIO_START_FRAMES_STEP;
}

int16_t settingsStartFramesToX(uint32_t frames)
{
    if (frames < USB_AUDIO_START_FRAMES_MIN) frames = USB_AUDIO_START_FRAMES_MIN;
    if (frames > USB_AUDIO_START_FRAMES_MAX) frames = USB_AUDIO_START_FRAMES_MAX;
    return static_cast<int16_t>(SETTINGS_BUFFER_SLIDER_X +
        ((frames - USB_AUDIO_START_FRAMES_MIN) *
         SETTINGS_BUFFER_SLIDER_WIDTH) /
        (USB_AUDIO_START_FRAMES_MAX - USB_AUDIO_START_FRAMES_MIN));
}

Rect clippedRect(const Rect& source, const Rect& clip)
{
    int16_t left = source.x > clip.x ? source.x : clip.x;
    int16_t top = source.y > clip.y ? source.y : clip.y;
    int16_t right = source.right() < clip.right() ?
                    source.right() : clip.right();
    int16_t bottom = source.bottom() < clip.bottom() ?
                     source.bottom() : clip.bottom();
    if (right <= left || bottom <= top) return Rect(0, 0, 0, 0);
    return Rect(left, top, static_cast<int16_t>(right - left),
                static_cast<int16_t>(bottom - top));
}

void formatStopwatchTicks(uint32_t ticks, char output[12])
{
    const uint32_t centiseconds = static_cast<uint32_t>(
        (static_cast<uint64_t>(ticks) * 100U) / RTC_LSE_TICK_HZ);
    const uint32_t hours = (centiseconds / 360000U) % 100U;
    const uint32_t minutes = (centiseconds / 6000U) % 60U;
    const uint32_t seconds = (centiseconds / 100U) % 60U;
    const uint32_t hundredths = centiseconds % 100U;
    output[0] = static_cast<char>('0' + hours / 10U);
    output[1] = static_cast<char>('0' + hours % 10U);
    output[2] = ':';
    output[3] = static_cast<char>('0' + minutes / 10U);
    output[4] = static_cast<char>('0' + minutes % 10U);
    output[5] = ':';
    output[6] = static_cast<char>('0' + seconds / 10U);
    output[7] = static_cast<char>('0' + seconds % 10U);
    output[8] = '.';
    output[9] = static_cast<char>('0' + hundredths / 10U);
    output[10] = static_cast<char>('0' + hundredths % 10U);
    output[11] = 0;
}

void formatLapNumber(uint8_t number, char output[8])
{
    output[0] = 'L'; output[1] = 'A'; output[2] = 'P'; output[3] = ' ';
    output[4] = static_cast<char>('0' + (number / 100U) % 10U);
    output[5] = static_cast<char>('0' + (number / 10U) % 10U);
    output[6] = static_cast<char>('0' + number % 10U);
    output[7] = 0;
}

void formatLapCount(uint8_t count, char output[9])
{
    output[0] = static_cast<char>('0' + (count / 10U) % 10U);
    output[1] = static_cast<char>('0' + count % 10U);
    output[2] = ' ';
    output[3] = 'S'; output[4] = 'A'; output[5] = 'V'; output[6] = 'E'; output[7] = 'D';
    output[8] = 0;
}

uint8_t textLength(const char* text)
{
    uint8_t length = 0U;
    while (text[length] != 0) ++length;
    return length;
}

char* appendUnsigned(char* output, uint32_t value)
{
    char reverse[10];
    uint8_t count = 0U;
    do {
        reverse[count++] = static_cast<char>('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    while (count > 0U) *output++ = reverse[--count];
    return output;
}

char* appendUnsigned64(char* output, uint64_t value)
{
    char reverse[20];
    uint8_t count = 0U;
    do {
        reverse[count++] = static_cast<char>('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    while (count > 0U) *output++ = reverse[--count];
    return output;
}

bool formatCalculatorValue(double value, char output[24])
{
    bool negative = value < 0.0;
    double magnitude = negative ? -value : value;
    if (magnitude > 999999999999.0) {
        std::strcpy(output, "ERROR");
        return false;
    }

    const uint64_t scaled = static_cast<uint64_t>(magnitude * 1000000.0 + 0.5);
    const uint64_t whole = scaled / 1000000ULL;
    uint32_t fraction = static_cast<uint32_t>(scaled % 1000000ULL);
    char* cursor = output;
    if (negative && scaled != 0ULL) *cursor++ = '-';
    cursor = appendUnsigned64(cursor, whole);
    if (fraction != 0U) {
        *cursor++ = '.';
        uint32_t divisor = 100000U;
        char* fractionStart = cursor;
        for (uint8_t digit = 0U; digit < 6U; ++digit) {
            *cursor++ = static_cast<char>('0' + (fraction / divisor) % 10U);
            divisor /= 10U;
        }
        while (cursor > fractionStart && cursor[-1] == '0') --cursor;
    }
    *cursor = 0;
    return true;
}

char calculatorKeyAt(int16_t x, int16_t y)
{
    static const char keys[4][4] = {
        {'C', 'S', 'B', '/'},
        {'7', '8', '9', '*'},
        {'4', '5', '6', '-'},
        {'1', '2', '3', '+'}
    };
    /* Landscape calculator: display on the left, five tall keypad rows on
       the right. Keep hit boxes identical to the painted button geometry. */
    if (x >= 340 && y >= 120 && y < 427) {
        const int16_t row = static_cast<int16_t>((y - 120) / 63);
        if (((y - 120) % 63) >= 55 || row > 4) return 0;
        if (row == 4) {
            if (x >= 340 && x < 551) return '0';
            if (x >= 558 && x < 660) return '.';
            if (x >= 667 && x < 769) return '=';
            return 0;
        }
        for (int16_t col = 0; col < 4; ++col) {
            const int16_t left = static_cast<int16_t>(340 + col * 109);
            if (x >= left && x < left + 102) return keys[row][col];
        }
    }
    return 0;
}

Rect calculatorKeyGlowRect(char key)
{
    static const char keys[4][4] = {
        {'C', 'S', 'B', '/'},
        {'7', '8', '9', '*'},
        {'4', '5', '6', '-'},
        {'1', '2', '3', '+'}
    };
    for (uint8_t row = 0U; row < 4U; ++row) {
        for (uint8_t col = 0U; col < 4U; ++col) {
            if (keys[row][col] == key) {
                return Rect(static_cast<int16_t>(336 + col * 109),
                            static_cast<int16_t>(116 + row * 63), 110, 63);
            }
        }
    }
    if (key == '0') return Rect(336, 368, 219, 63);
    if (key == '.') return Rect(554, 368, 110, 63);
    if (key == '=') return Rect(663, 368, 110, 63);
    return Rect();
}

char* appendText(char* output, const char* text)
{
    while (*text != 0) *output++ = *text++;
    return output;
}

void formatPercent(uint32_t used, uint32_t total, char output[5])
{
    uint32_t percent = total == 0U ? 0U :
        static_cast<uint32_t>((static_cast<uint64_t>(used) * 100U + total / 2U) / total);
    if (percent > 100U) percent = 100U;
    char* cursor = appendUnsigned(output, percent);
    *cursor++ = '%';
    *cursor = 0;
}

void formatSignedTenths(int16_t tenths, char output[12])
{
    char* cursor = output;
    int32_t value = tenths;
    if (value < 0) {
        *cursor++ = '-';
        value = -value;
    }
    cursor = appendUnsigned(cursor, static_cast<uint32_t>(value / 10));
    *cursor++ = '.';
    *cursor++ = static_cast<char>('0' + value % 10);
    *cursor = 0;
}

void formatUsage(uint32_t used, uint32_t total, char output[24])
{
    char* cursor = appendUnsigned(output, (used + 1023U) / 1024U);
    cursor = appendText(cursor, " KB / ");
    cursor = appendUnsigned(cursor, total / 1024U);
    cursor = appendText(cursor, " KB");
    *cursor = 0;
}

void formatHeapValue(const char* label, uint32_t bytes, char output[24])
{
    char* cursor = appendText(output, label);
    cursor = appendUnsigned(cursor, bytes / 1024U);
    cursor = appendText(cursor, " KB");
    *cursor = 0;
}

char* appendStorageAmount(char* output, uint32_t kb)
{
    if (kb >= (1024U * 1024U)) {
        const uint32_t tenths = static_cast<uint32_t>(
            (static_cast<uint64_t>(kb) * 10U + (1024U * 1024U) / 2U) /
            (1024U * 1024U));
        output = appendUnsigned(output, tenths / 10U);
        *output++ = '.';
        *output++ = static_cast<char>('0' + tenths % 10U);
        return appendText(output, " GB");
    }
    if (kb >= 1024U) {
        output = appendUnsigned(output, (kb + 512U) / 1024U);
        return appendText(output, " MB");
    }
    output = appendUnsigned(output, kb);
    return appendText(output, " KB");
}

void formatStorageSummary(uint32_t freeKb, uint32_t totalKb, char output[32])
{
    const uint32_t usedKb = totalKb >= freeKb ? totalKb - freeKb : 0U;
    char* cursor = appendText(output, "USED ");
    cursor = appendStorageAmount(cursor, usedKb);
    cursor = appendText(cursor, " / ");
    cursor = appendStorageAmount(cursor, totalKb);
    *cursor = 0;
}

void formatStorageFree(uint32_t freeKb, char output[20])
{
    char* cursor = appendText(output, "FREE ");
    cursor = appendStorageAmount(cursor, freeKb);
    *cursor = 0;
}

void formatStoragePercent(uint32_t usedKb, uint32_t totalKb, char output[6])
{
    uint32_t tenths = totalKb == 0U ? 0U : static_cast<uint32_t>(
        (static_cast<uint64_t>(usedKb) * 1000U + totalKb / 2U) / totalKb);
    if (tenths >= 1000U) {
        output[0] = '1'; output[1] = '0'; output[2] = '0'; output[3] = '%'; output[4] = 0;
        return;
    }
    char* cursor = appendUnsigned(output, tenths / 10U);
    *cursor++ = '.';
    *cursor++ = static_cast<char>('0' + tenths % 10U);
    *cursor++ = '%';
    *cursor = 0;
}

void formatFileSize(uint32_t bytes, char output[16])
{
    char* cursor;
    if (bytes >= (1024U * 1024U)) {
        cursor = appendUnsigned(output, bytes / (1024U * 1024U));
        cursor = appendText(cursor, " MB");
    } else if (bytes >= 1024U) {
        cursor = appendUnsigned(output, (bytes + 1023U) / 1024U);
        cursor = appendText(cursor, " KB");
    } else {
        cursor = appendUnsigned(output, bytes);
        cursor = appendText(cursor, " B");
    }
    *cursor = 0;
}

void formatRecorderTime(uint32_t seconds, char output[9])
{
    const uint32_t hours = (seconds / 3600U) % 100U;
    const uint32_t minutes = (seconds / 60U) % 60U;
    const uint32_t secs = seconds % 60U;
    output[0] = static_cast<char>('0' + hours / 10U);
    output[1] = static_cast<char>('0' + hours % 10U);
    output[2] = ':';
    output[3] = static_cast<char>('0' + minutes / 10U);
    output[4] = static_cast<char>('0' + minutes % 10U);
    output[5] = ':';
    output[6] = static_cast<char>('0' + secs / 10U);
    output[7] = static_cast<char>('0' + secs % 10U);
    output[8] = 0;
}

void formatPlayerTime(uint32_t milliseconds, char output[6])
{
    uint32_t seconds = milliseconds / 1000U;
    uint32_t minutes = seconds / 60U;
    if (minutes > 99U) minutes = 99U;
    seconds %= 60U;
    output[0] = static_cast<char>('0' + minutes / 10U);
    output[1] = static_cast<char>('0' + minutes % 10U);
    output[2] = ':';
    output[3] = static_cast<char>('0' + seconds / 10U);
    output[4] = static_cast<char>('0' + seconds % 10U);
    output[5] = 0;
}

bool isWavEntry(const SD_StorageEntry& entry)
{
    if (entry.is_directory != 0U) return false;
    uint8_t length = textLength(entry.name);
    if (length < 4U) return false;
    char a = entry.name[length - 3U];
    char b = entry.name[length - 2U];
    char c = entry.name[length - 1U];
    if (a >= 'a' && a <= 'z') a = static_cast<char>(a - 'a' + 'A');
    if (b >= 'a' && b <= 'z') b = static_cast<char>(b - 'a' + 'A');
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return entry.name[length - 4U] == '.' && a == 'W' && b == 'A' && c == 'V';
}

uint8_t wavEntryCount(const SD_StorageSnapshot& snapshot)
{
    uint8_t count = 0U;
    for (uint8_t index = 0U; index < snapshot.entry_count; ++index) {
        if (isWavEntry(snapshot.entries[index])) ++count;
    }
    return count;
}

const SD_StorageEntry* wavEntryAt(const SD_StorageSnapshot& snapshot, uint8_t ordinal)
{
    for (uint8_t index = 0U; index < snapshot.entry_count; ++index) {
        if (!isWavEntry(snapshot.entries[index])) continue;
        if (ordinal == 0U) return &snapshot.entries[index];
        --ordinal;
    }
    return 0;
}

void getSystemMemoryUsage(uint32_t& ramUsed, uint32_t& romUsed,
                          uint32_t& heapFree, uint32_t& heapMinimum)
{
    const uint32_t ramTotal = 512U * 1024U;
    const uint32_t linkedRam = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(&_end) - 0x20000000UL +
        reinterpret_cast<uintptr_t>(&_Min_Heap_Size) +
        reinterpret_cast<uintptr_t>(&_Min_Stack_Size));
    heapFree = static_cast<uint32_t>(xPortGetFreeHeapSize());
    heapMinimum = static_cast<uint32_t>(xPortGetMinimumEverFreeHeapSize());
    const uint32_t heapUsed = static_cast<uint32_t>(configTOTAL_HEAP_SIZE) - heapFree;
    ramUsed = linkedRam > static_cast<uint32_t>(configTOTAL_HEAP_SIZE) ?
        linkedRam - static_cast<uint32_t>(configTOTAL_HEAP_SIZE) + heapUsed : linkedRam;
    if (ramUsed > ramTotal) ramUsed = ramTotal;

    romUsed = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(&_sidata) - 0x08000000UL +
        (reinterpret_cast<uintptr_t>(&_edata) - reinterpret_cast<uintptr_t>(&_sdata)));
}

const uint8_t* glyphFor(char input)
{
    static const uint8_t blank[7] = {0,0,0,0,0,0,0};
    static const uint8_t glyphs[][7] = {
        {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
        {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
        {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
        {14,17,16,23,17,17,14}, {17,17,17,31,17,17,17},
        {31,4,4,4,4,4,31}, {7,2,2,2,2,18,12},
        {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
        {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
        {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
        {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
        {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}
    };
    static const uint8_t digits[][7] = {
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14}
    };
    static const uint8_t colon[7] = {0,4,4,0,4,4,0};
    static const uint8_t dash[7] = {0,0,0,31,0,0,0};
    static const uint8_t dot[7] = {0,0,0,0,0,6,6};
    static const uint8_t slash[7] = {1,2,2,4,8,8,16};
    static const uint8_t percent[7] = {17,2,4,4,8,16,17};
    static const uint8_t underscore[7] = {0,0,0,0,0,0,31};
    static const uint8_t tilde[7] = {0,0,9,22,0,0,0};
    static const uint8_t plus[7] = {0,4,4,31,4,4,0};
    static const uint8_t equal[7] = {0,31,0,31,0,0,0};
    static const uint8_t star[7] = {0,17,10,4,10,17,0};
    static const uint8_t leftParenthesis[7] = {2,4,8,8,8,4,2};
    static const uint8_t rightParenthesis[7] = {8,4,2,2,2,4,8};

    char c = input;
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return glyphs[c - 'A'];
    if (c >= '0' && c <= '9') return digits[c - '0'];
    if (c == ':') return colon;
    if (c == '-') return dash;
    if (c == '.') return dot;
    if (c == '/') return slash;
    if (c == '%') return percent;
    if (c == '_') return underscore;
    if (c == '~') return tilde;
    if (c == '+') return plus;
    if (c == '=') return equal;
    if (c == '*') return star;
    if (c == '(') return leftParenthesis;
    if (c == ')') return rightParenthesis;
    return blank;
}
}

void ProceduralWidget::fillLocal(const Rect& bounds, const Rect& dirty, colortype color) const
{
    Rect clipped = bounds;
    clipped &= dirty;
    if (clipped.width <= 0 || clipped.height <= 0) return;
    translateRectToAbsolute(clipped);

    /* DMA2D has setup overhead, so keep tiny font pixels on the CPU and send
       larger RGB888 spans/rectangles directly to the hardware raster engine. */
    const uint32_t pixels = static_cast<uint32_t>(clipped.width) * clipped.height;
    if (pixels >= 48U && clipped.x >= 0 && clipped.y >= 0 &&
        clipped.right() <= 800 && clipped.bottom() <= 480) {
        /* TouchGFX chooses the buffer opposite the one currently transferred
           by LTDC/DSI. Never hard-code framebuffer 0 in double-buffer mode. */
        const uint32_t clientBuffer = TouchGFX_GetClientFrameBuffer();
        const uint32_t destination = clientBuffer +
            (static_cast<uint32_t>(clipped.y) * LCD_FRAME_BUFFER_STRIDE_PIXELS +
             clipped.x) * LCD_BYTES_PER_PIXEL;
        if (TouchGFX_DMA2D_FillRGB888(destination,
                                     static_cast<uint16_t>(LCD_FRAME_BUFFER_STRIDE_PIXELS -
                                                           clipped.width),
                                     static_cast<uint16_t>(clipped.width),
                                     static_cast<uint16_t>(clipped.height),
                                     static_cast<uint32_t>(color)) == HAL_OK) {
            return;
        }
    }
    HAL::lcd().fillRect(clipped, color);
}

void ProceduralWidget::fillRounded(int16_t x, int16_t y, int16_t w, int16_t h,
                                   int16_t radius, colortype color, const Rect& dirty) const
{
    if (w <= 0 || h <= 0) return;
    if (radius < 1) {
        fillLocal(Rect(x, y, w, h), dirty, color);
        return;
    }
    if (radius * 2 > w) radius = w / 2;
    if (radius * 2 > h) radius = h / 2;

    /* The original rounded approximation used only two corner insets, but
       submitted each scanline as a separate DMA2D operation. Batch identical
       rows into bands: the same shape now needs at most five fills. */
    fillLocal(Rect(x, y + radius, w, h - 2 * radius), dirty, color);
    int16_t outerRows = 0;
    for (int16_t row = 0; row < radius; ++row) {
        const int16_t d = radius - row;
        if (d * d > radius * radius / 2) ++outerRows;
        else break;
    }
    if (outerRows > 0) {
        const int16_t inset = radius / 3;
        fillLocal(Rect(x + inset, y, w - 2 * inset, outerRows), dirty, color);
        fillLocal(Rect(x + inset, y + h - outerRows, w - 2 * inset, outerRows), dirty, color);
    }
    const int16_t innerRows = radius - outerRows;
    if (innerRows > 0) {
        fillLocal(Rect(x + 1, y + outerRows, w - 2, innerRows), dirty, color);
        fillLocal(Rect(x + 1, y + h - radius, w - 2, innerRows), dirty, color);
    }
}

void ProceduralWidget::drawText(const char* text, int16_t x, int16_t y, uint8_t scale,
                                colortype color, const Rect& dirty) const
{
    if (!text || scale == 0) return;
    int16_t cursor = x;
    while (*text) {
        const uint8_t* glyph = glyphFor(*text++);
        for (uint8_t row = 0; row < 7; ++row) {
            uint8_t col = 0;
            while (col < 5U) {
                if ((glyph[row] & (1U << (4U - col))) == 0U) {
                    ++col;
                    continue;
                }
                const uint8_t runStart = col;
                do {
                    ++col;
                } while (col < 5U && (glyph[row] & (1U << (4U - col))) != 0U);
                fillLocal(Rect(cursor + runStart * scale, y + row * scale,
                               static_cast<int16_t>((col - runStart) * scale), scale),
                          dirty, color);
            }
        }
        cursor += static_cast<int16_t>(6 * scale);
    }
}

void ProceduralWidget::drawCloseX(int16_t cx, int16_t cy, colortype color, const Rect& dirty) const
{
    for (int16_t i = -10; i <= 10; ++i) {
        fillLocal(Rect(cx + i - 1, cy + i - 1, 3, 3), dirty, color);
        fillLocal(Rect(cx + i - 1, cy - i - 1, 3, 3), dirty, color);
    }
}

DesktopBackgroundWidget::DesktopBackgroundWidget() : currentPage(0) {}

void DesktopBackgroundWidget::setPage(uint8_t page)
{
    if (currentPage != page) {
        currentPage = page;
        invalidate();
    }
}

void DesktopBackgroundWidget::draw(const Rect& dirty) const
{
    /* The gradient bands below overwrite every pixel on a full-screen dirty
       frame. Tell the Video-Mode double-buffer path not to copy the old front
       buffer first; this is the common path while the desktop is sliding. */
    if (dirty.x <= 0 && dirty.y <= 0 &&
        dirty.right() >= 800 && dirty.bottom() >= 480) {
        TouchGFX_BeginFullScreenRedraw();
    }

    const int16_t firstBand = static_cast<int16_t>((dirty.y / 8) * 8);
    for (int16_t y = firstBand; y < dirty.bottom(); y += 8) {
        const int16_t sampleY = static_cast<int16_t>(y + 4);
        const uint8_t r = static_cast<uint8_t>(18 + sampleY / 22);
        const uint8_t g = static_cast<uint8_t>(24 + sampleY / 28);
        const uint8_t b = static_cast<uint8_t>(58 + sampleY / 8);
        fillLocal(Rect(0, y, 800, 8), dirty, rgb(r, g, b));
    }
    fillLocal(Rect(0, 0, 800, 54), dirty, rgb(12, 18, 42));
    drawText("11:42", 22, 18, 2, rgb(236, 242, 255), dirty);
    drawText("TOUCHGFX DESKTOP", 296, 18, 2, rgb(170, 188, 224), dirty);
    for (uint8_t i = 0; i < 4; ++i) {
        fillLocal(Rect(688 + i * 7, 34 - i * 4, 4, 4 + i * 4), dirty, rgb(235, 242, 255));
    }
    fillRounded(742, 17, 38, 19, 4, rgb(103, 118, 155), dirty);
    fillLocal(Rect(746, 21, 28, 11), dirty, rgb(105, 230, 151));
    fillLocal(Rect(780, 22, 3, 9), dirty, rgb(103, 118, 155));
    drawText("MY APPS", 24, 72, 3, rgb(245, 248, 255), dirty);
    drawText(currentPage == 0 ? "PAGE 1" : "PAGE 2", 686, 76, 2, rgb(154, 175, 216), dirty);

    fillRounded(383, 448, 14, 14, 7, currentPage == 0 ? rgb(248, 250, 255) : rgb(91, 108, 151), dirty);
    fillRounded(405, 448, 14, 14, 7, currentPage == 1 ? rgb(248, 250, 255) : rgb(91, 108, 151), dirty);
}

DesktopIconWidget::DesktopIconWidget() : appIndex(0), label("APP"), accent(rgb(70, 120, 220)) {}

void DesktopIconWidget::configure(uint8_t index, const char* name, uint8_t r, uint8_t g, uint8_t b)
{
    appIndex = index;
    label = name;
    accent = rgb(r, g, b);
}

void DesktopIconWidget::drawSymbol(const Rect& dirty) const
{
    const colortype white = rgb(250, 252, 255);
    const colortype soft = rgb(221, 232, 251);
    switch (appIndex) {
    case 0:
        fillRounded(42, 17, 18, 25, 8, rgb(255, 219, 70), dirty);
        fillRounded(57, 31, 25, 18, 8, rgb(91, 220, 143), dirty);
        fillRounded(42, 46, 18, 25, 8, rgb(77, 164, 255), dirty);
        fillRounded(25, 31, 25, 18, 8, rgb(251, 101, 138), dirty);
        fillRounded(46, 35, 12, 12, 6, white, dirty);
        break;
    case 1:
        fillLocal(Rect(53, 20, 5, 37), dirty, white);
        fillLocal(Rect(57, 19, 18, 5), dirty, white);
        fillRounded(36, 51, 20, 15, 7, white, dirty);
        fillRounded(58, 43, 20, 15, 7, white, dirty);
        break;
    case 2:
        fillRounded(25, 30, 55, 37, 7, rgb(230, 245, 255), dirty);
        fillRounded(29, 23, 25, 16, 5, rgb(230, 245, 255), dirty);
        fillLocal(Rect(31, 40, 43, 4), dirty, rgb(87, 171, 244));
        break;
    case 3:
        fillRounded(29, 17, 47, 55, 6, white, dirty);
        for (uint8_t i = 0; i < 4; ++i) fillLocal(Rect(37, 29 + i * 9, 30, 3), dirty, rgb(224, 169, 48));
        break;
    case 4:
        fillRounded(27, 17, 50, 50, 25, white, dirty);
        fillRounded(32, 22, 40, 40, 20, rgb(66, 79, 101), dirty);
        fillLocal(Rect(50, 31, 4, 14), dirty, white);
        fillLocal(Rect(52, 43, 12, 4), dirty, white);
        break;
    case 5:
        fillRounded(29, 18, 26, 26, 13, rgb(255, 218, 77), dirty);
        fillRounded(37, 40, 42, 22, 10, white, dirty);
        fillRounded(27, 45, 27, 17, 8, soft, dirty);
        break;
    case 6:
        fillRounded(27, 13, 52, 60, 9, rgb(38, 44, 57), dirty);
        fillRounded(33, 19, 40, 13, 3, rgb(224, 235, 224), dirty);
        fillLocal(Rect(38, 25, 30, 2), dirty, rgb(75, 87, 82));
        for (uint8_t row = 0U; row < 3U; ++row) {
            for (uint8_t col = 0U; col < 3U; ++col) {
                const colortype keyColor = col == 2U ? rgb(255, 177, 66) :
                                                       rgb(226, 231, 239);
                fillRounded(static_cast<int16_t>(34 + col * 13),
                            static_cast<int16_t>(38 + row * 11),
                            9, 8, 2, keyColor, dirty);
            }
        }
        break;
    case 7:
        fillRounded(38, 17, 30, 42, 15, white, dirty);
        fillRounded(44, 23, 18, 18, 9, accent, dirty);
        fillLocal(Rect(49, 51, 8, 15), dirty, white);
        break;
    case 8:
        fillRounded(22, 28, 62, 39, 8, rgb(228, 234, 245), dirty);
        fillRounded(42, 31, 25, 25, 12, rgb(65, 76, 96), dirty);
        fillRounded(48, 37, 13, 13, 6, rgb(132, 184, 232), dirty);
        fillLocal(Rect(31, 22, 18, 8), dirty, white);
        break;
    case 9:
        fillRounded(24, 17, 57, 52, 7, white, dirty);
        fillLocal(Rect(24, 17, 57, 15), dirty, rgb(232, 72, 76));
        drawText("12", 39, 39, 3, rgb(67, 75, 94), dirty);
        break;
    case 10:
        fillRounded(25, 17, 57, 54, 8, rgb(19, 49, 44), dirty);
        for (uint8_t bar = 0U; bar < 7U; ++bar) {
            const int16_t height = static_cast<int16_t>(10 + (bar * 17U) % 37U);
            fillRounded(static_cast<int16_t>(31 + bar * 7),
                        static_cast<int16_t>(64 - height),
                        4, height, 2,
                        bar > 4U ? rgb(249, 196, 73) : white, dirty);
        }
        break;
    case 11:
        fillRounded(29, 18, 48, 48, 24, soft, dirty);
        fillRounded(39, 28, 28, 28, 14, rgb(104, 118, 141), dirty);
        fillRounded(47, 36, 12, 12, 6, soft, dirty);
        break;
    case 12:
        fillRounded(24, 21, 28, 45, 5, white, dirty);
        fillRounded(53, 21, 28, 45, 5, rgb(244, 229, 210), dirty);
        fillLocal(Rect(51, 24, 3, 40), dirty, rgb(193, 111, 49));
        break;
    case 13:
        fillRounded(39, 15, 28, 43, 14, white, dirty);
        fillRounded(45, 21, 16, 31, 8, accent, dirty);
        fillLocal(Rect(31, 36, 5, 10), dirty, white);
        fillLocal(Rect(70, 36, 5, 10), dirty, white);
        fillRounded(31, 42, 44, 22, 11, white, dirty);
        fillRounded(36, 42, 34, 16, 8, accent, dirty);
        fillLocal(Rect(50, 60, 6, 8), dirty, white);
        fillRounded(40, 66, 26, 4, 2, white, dirty);
        break;
    case 14:
        fillRounded(24, 20, 57, 47, 9, white, dirty);
        for (int16_t row = 0; row < 24; ++row) fillLocal(Rect(45, 31 + row, 2 + row / 2, 1), dirty, accent);
        break;
    case 15:
        fillRounded(26, 18, 54, 52, 9, white, dirty);
        fillRounded(34, 26, 38, 36, 6, accent, dirty);
        drawText("M", 44, 34, 3, white, dirty);
        for (uint8_t i = 0U; i < 4U; ++i) {
            fillLocal(Rect(20, 25 + i * 11, 7, 3), dirty, white);
            fillLocal(Rect(79, 25 + i * 11, 7, 3), dirty, white);
        }
        break;
    default:
        fillRounded(29, 22, 25, 25, 12, white, dirty);
        fillRounded(52, 22, 25, 25, 12, white, dirty);
        for (int16_t row = 0; row < 26; ++row) fillLocal(Rect(31 + row / 2, 35 + row, 44 - row, 2), dirty, white);
        break;
    }
}

void DesktopIconWidget::draw(const Rect& dirty) const
{
    fillRounded(21, 8, 70, 70, 15, rgb(8, 13, 36), dirty);
    fillRounded(17, 4, 70, 70, 15, accent, dirty);
    fillLocal(Rect(23, 9, 58, 2), dirty, rgb(255, 255, 255));
    drawSymbol(dirty);

    uint8_t len = 0;
    while (label[len]) ++len;
    if (len > 8U) {
        /* Keep long names at the same two-pixel glyph scale as every other
           app. Only tighten the inter-character advance so STOPWATCH and
           CALCULATOR fit beneath the icon. */
        const int16_t advance = 10;
        int16_t labelX = static_cast<int16_t>((104 - len * advance) / 2);
        char character[2] = {0, 0};
        for (uint8_t index = 0U; index < len; ++index) {
            character[0] = label[index];
            drawText(character, static_cast<int16_t>(labelX + index * advance),
                     87, 2, rgb(243, 247, 255), dirty);
        }
    } else {
        const int16_t labelX = static_cast<int16_t>((104 - len * 12) / 2);
        drawText(label, labelX, 87, 2, rgb(243, 247, 255), dirty);
    }
}

AppPanelWidget::AppPanelWidget()
    : appIndex(0), label("APP"), accentR(68), accentG(115), accentB(220),
      stopwatchLapScroll(0), fileScroll(0), musicScroll(0), musicDetail(0),
      settingsScroll(0),
      calculatorLength(1U), calculatorAccumulator(0.0), calculatorOperation(0),
      calculatorPressedKey(0), calculatorReplaceInput(false), calculatorError(false)
{
    resetCalculator();
}

void AppPanelWidget::configure(uint8_t index, const char* name, uint8_t r, uint8_t g, uint8_t b)
{
    appIndex = index;
    label = name;
    accentR = r;
    accentG = g;
    accentB = b;
    invalidate();
}

void AppPanelWidget::invalidateStopwatchTime()
{
    Rect timeArea(225, 135, 350, 42);
    invalidateRect(timeArea);
}

void AppPanelWidget::invalidateStopwatchStartButton()
{
    Rect statusArea(615, 120, 140, 31);
    Rect controlsArea(30, 214, 740, 56);
    invalidateRect(statusArea);
    invalidateRect(controlsArea);
}

void AppPanelWidget::invalidateStopwatchLaps()
{
    Rect lapArea(30, 282, 740, 162);
    invalidateRect(lapArea);
}

void AppPanelWidget::invalidateStopwatchBody()
{
    Rect stopwatchBody(30, 112, 740, 332);
    invalidateRect(stopwatchBody);
}

void AppPanelWidget::invalidateSystemUsage()
{
    Rect systemArea(35, 130, 730, 286);
    invalidateRect(systemArea);
}

void AppPanelWidget::invalidateRecorder()
{
    Rect recorderArea(30, 112, 740, 326);
    invalidateRect(recorderArea);
}

void AppPanelWidget::invalidateCalculator()
{
    Rect calculatorArea(30, 112, 740, 320);
    invalidateRect(calculatorArea);
}

void AppPanelWidget::invalidateSpectrum()
{
    Rect spectrumArea(25, 108, 750, 336);
    invalidateRect(spectrumArea);
}

void AppPanelWidget::invalidateSettings()
{
    Rect settingsArea(20, SETTINGS_VIEW_TOP, 765,
                      SETTINGS_VIEW_BOTTOM - SETTINGS_VIEW_TOP);
    invalidateRect(settingsArea);
}

void AppPanelWidget::invalidateSettingsGain()
{
    Rect gainArea(110, static_cast<int16_t>(364 - settingsScroll), 620, 73);
    invalidateRect(gainArea);
}

void AppPanelWidget::invalidateSettingsStartBuffer()
{
    Rect bufferArea(115, static_cast<int16_t>(598 - settingsScroll), 570, 104);
    invalidateRect(bufferArea);
}

void AppPanelWidget::resetCalculator()
{
    calculatorInput[0] = '0';
    calculatorInput[1] = 0;
    calculatorHistory[0] = 0;
    calculatorLength = 1U;
    calculatorAccumulator = 0.0;
    calculatorOperation = 0;
    calculatorPressedKey = 0;
    calculatorReplaceInput = false;
    calculatorError = false;
}

void AppPanelWidget::setCalculatorPressedKey(char key)
{
    if (calculatorPressedKey == key) return;
    Rect oldArea = calculatorKeyGlowRect(calculatorPressedKey);
    calculatorPressedKey = key;
    Rect newArea = calculatorKeyGlowRect(calculatorPressedKey);
    /* Redraw only the old and new key, including the four-pixel glow. */
    if (oldArea.width > 0 && oldArea.height > 0) invalidateRect(oldArea);
    if (newArea.width > 0 && newArea.height > 0) invalidateRect(newArea);
}

double AppPanelWidget::calculatorValue() const
{
    uint8_t index = 0U;
    bool negative = false;
    double value = 0.0;
    double fraction = 0.1;
    bool afterDecimal = false;
    if (calculatorInput[index] == '-') {
        negative = true;
        ++index;
    }
    while (calculatorInput[index] != 0) {
        const char current = calculatorInput[index++];
        if (current == '.') {
            afterDecimal = true;
        } else if (current >= '0' && current <= '9') {
            if (afterDecimal) {
                value += static_cast<double>(current - '0') * fraction;
                fraction *= 0.1;
            } else {
                value = value * 10.0 + static_cast<double>(current - '0');
            }
        }
    }
    return negative ? -value : value;
}

bool AppPanelWidget::applyCalculatorOperation(double right)
{
    double result = calculatorAccumulator;
    if (calculatorOperation == '+') result += right;
    else if (calculatorOperation == '-') result -= right;
    else if (calculatorOperation == '*') result *= right;
    else if (calculatorOperation == '/') {
        if (right > -0.000000000001 && right < 0.000000000001) {
            std::strcpy(calculatorInput, "ERROR");
            calculatorLength = 5U;
            calculatorError = true;
            return false;
        }
        result /= right;
    }

    if (!formatCalculatorValue(result, calculatorInput)) {
        calculatorLength = 5U;
        calculatorError = true;
        return false;
    }
    calculatorLength = textLength(calculatorInput);
    calculatorAccumulator = result;
    return true;
}

void AppPanelWidget::calculatorPress(char key)
{
    if (key == 0) return;
    if (key == 'C') {
        resetCalculator();
        invalidateCalculator();
        return;
    }
    if (calculatorError) {
        resetCalculator();
        if (!((key >= '0' && key <= '9') || key == '.')) {
            invalidateCalculator();
            return;
        }
    }

    if (key >= '0' && key <= '9') {
        if (calculatorReplaceInput) {
            calculatorInput[0] = '0';
            calculatorInput[1] = 0;
            calculatorLength = 1U;
            calculatorReplaceInput = false;
            if (calculatorOperation == 0) calculatorHistory[0] = 0;
        }
        if (calculatorLength == 1U && calculatorInput[0] == '0') {
            calculatorInput[0] = key;
        } else if (calculatorLength < sizeof(calculatorInput) - 1U) {
            calculatorInput[calculatorLength++] = key;
            calculatorInput[calculatorLength] = 0;
        }
    } else if (key == '.') {
        if (calculatorReplaceInput) {
            calculatorInput[0] = '0';
            calculatorInput[1] = 0;
            calculatorLength = 1U;
            calculatorReplaceInput = false;
            if (calculatorOperation == 0) calculatorHistory[0] = 0;
        }
        if (std::strchr(calculatorInput, '.') == 0 &&
            calculatorLength < sizeof(calculatorInput) - 1U) {
            calculatorInput[calculatorLength++] = '.';
            calculatorInput[calculatorLength] = 0;
        }
    } else if (key == 'S') {
        if (!(calculatorLength == 1U && calculatorInput[0] == '0')) {
            if (calculatorInput[0] == '-') {
                std::memmove(calculatorInput, calculatorInput + 1U,
                             calculatorLength);
                --calculatorLength;
            } else if (calculatorLength < sizeof(calculatorInput) - 1U) {
                std::memmove(calculatorInput + 1U, calculatorInput,
                             calculatorLength + 1U);
                calculatorInput[0] = '-';
                ++calculatorLength;
            }
        }
    } else if (key == 'B') {
        calculatorReplaceInput = false;
        if (calculatorLength > 1U) {
            --calculatorLength;
            calculatorInput[calculatorLength] = 0;
            if ((calculatorLength == 1U && calculatorInput[0] == '-') ||
                (calculatorLength == 2U && calculatorInput[0] == '-' &&
                 calculatorInput[1] == '0')) {
                calculatorInput[0] = '0';
                calculatorInput[1] = 0;
                calculatorLength = 1U;
            }
        } else {
            calculatorInput[0] = '0';
            calculatorInput[1] = 0;
            calculatorLength = 1U;
        }
    } else if (key == '+' || key == '-' || key == '*' || key == '/') {
        if (calculatorOperation != 0 && !calculatorReplaceInput) {
            if (!applyCalculatorOperation(calculatorValue())) {
                invalidateCalculator();
                return;
            }
        } else if (calculatorOperation == 0) {
            calculatorAccumulator = calculatorValue();
        }
        calculatorOperation = key;
        char* cursor = calculatorHistory;
        char value[24];
        (void)formatCalculatorValue(calculatorAccumulator, value);
        cursor = appendText(cursor, value);
        *cursor++ = ' ';
        *cursor++ = key;
        *cursor = 0;
        calculatorReplaceInput = true;
    } else if (key == '=' && calculatorOperation != 0) {
        const double right = calculatorValue();
        char leftText[24];
        char rightText[24];
        (void)formatCalculatorValue(calculatorAccumulator, leftText);
        (void)formatCalculatorValue(right, rightText);
        char* cursor = calculatorHistory;
        cursor = appendText(cursor, leftText);
        *cursor++ = ' ';
        *cursor++ = calculatorOperation;
        *cursor++ = ' ';
        cursor = appendText(cursor, rightText);
        cursor = appendText(cursor, " =");
        *cursor = 0;
        if (applyCalculatorOperation(right)) {
            calculatorOperation = 0;
            calculatorReplaceInput = true;
        }
    }
    invalidateCalculator();
}

void AppPanelWidget::invalidateFiles()
{
    Rect filesArea(30, 108, 740, 336);
    invalidateRect(filesArea);
}

void AppPanelWidget::invalidateMusic()
{
    Rect musicArea(25, 96, 750, 348);
    invalidateRect(musicArea);
}

void AppPanelWidget::setStopwatchLapScroll(uint8_t offset)
{
    if (stopwatchLapScroll == offset) return;
    stopwatchLapScroll = offset;
    invalidateStopwatchLaps();
}

void AppPanelWidget::setFileScroll(uint8_t offset)
{
    if (fileScroll == offset) return;
    fileScroll = offset;
    invalidateFiles();
}

void AppPanelWidget::setMusicScroll(uint8_t offset)
{
    if (musicScroll == offset) return;
    musicScroll = offset;
    invalidateMusic();
}

void AppPanelWidget::setMusicDetail(uint8_t detail)
{
    if (musicDetail == detail) return;
    musicDetail = detail;
    invalidateMusic();
}

void AppPanelWidget::setSettingsScroll(int16_t offset)
{
    if (offset < 0) offset = 0;
    if (offset > SETTINGS_SCROLL_MAX) offset = SETTINGS_SCROLL_MAX;
    if (settingsScroll == offset) return;
    settingsScroll = offset;
    invalidateSettings();
}

void AppPanelWidget::drawStopwatch(const Rect& dirty) const
{
    const colortype white = rgb(244, 248, 255);
    const colortype muted = rgb(143, 160, 191);
    const colortype surface = rgb(20, 28, 43);
    const colortype rowSurface = rgb(29, 39, 57);
    const colortype cyan = rgb(62, 211, 177);
    const colortype blue = rgb(63, 139, 246);
    const colortype red = rgb(239, 91, 101);
    const colortype amber = rgb(244, 174, 71);
    char elapsed[12];
    formatStopwatchTicks(Stopwatch_GetTicks(), elapsed);

    const bool running = Stopwatch_IsRunning() != 0U;
    fillRounded(30, 112, 740, 92, 18, rgb(48, 63, 87), dirty);
    fillRounded(34, 116, 732, 84, 16, surface, dirty);
    fillRounded(48, 128, 4, 58, 2, running ? cyan : rgb(94, 111, 143), dirty);
    drawText("LSE 32.768 KHZ", 66, 125, 1, muted, dirty);
    drawText(elapsed, 235, 139, 5, white, dirty);
    drawText("RTC 1024 HZ PRECISION", 66, 181, 1, muted, dirty);
    fillRounded(622, 123, 119, 27, 13, running ? rgb(30, 100, 82) : rgb(61, 72, 94), dirty);
    drawText(running ? "RUNNING" : (Stopwatch_GetTicks() == 0U ? "READY" : "PAUSED"),
             running ? 660 : (Stopwatch_GetTicks() == 0U ? 667 : 664), 133, 1,
             running ? cyan : white, dirty);

    fillRounded(30, 214, 225, 56, 16, running ? red : cyan, dirty);
    drawText(running ? "PAUSE" : "START", running ? 93 : 99, 232, 3, white, dirty);
    fillRounded(287, 214, 225, 56, 16, running ? blue : rgb(61, 72, 94), dirty);
    drawText("LAP", 372, 232, 3, running ? white : muted, dirty);
    fillRounded(545, 214, 225, 56, 16, rgb(68, 80, 104), dirty);
    drawText("RESET", 607, 232, 3, white, dirty);

    const uint8_t lapCount = Stopwatch_GetLapCount();
    char saved[9];
    formatLapCount(lapCount, saved);
    fillRounded(30, 282, 740, 162, 16, rgb(16, 23, 36), dirty);
    fillRounded(34, 286, 732, 154, 14, surface, dirty);
    drawText("LAP", 52, 297, 1, muted, dirty);
    drawText("SPLIT", 240, 297, 1, muted, dirty);
    drawText("TOTAL", 488, 297, 1, muted, dirty);
    drawText(saved, 680, 297, 1, muted, dirty);
    fillLocal(Rect(45, 314, 700, 1), dirty, rgb(51, 64, 86));

    if (lapCount == 0U) {
        fillRounded(45, 324, 697, 98, 12, rowSurface, dirty);
        drawText("START THE TIMER THEN TAP LAP", 244, 360, 2, muted, dirty);
    } else {
        uint32_t fastest = 0xFFFFFFFFUL;
        uint32_t slowest = 0U;
        for (uint8_t i = 0U; i < lapCount; ++i) {
            const uint32_t split = Stopwatch_GetLapTicks(i);
            if (split < fastest) fastest = split;
            if (split > slowest) slowest = split;
        }

        const uint8_t maxScroll = lapCount > 4U ? static_cast<uint8_t>(lapCount - 4U) : 0U;
        const uint8_t first = stopwatchLapScroll > maxScroll ? maxScroll : stopwatchLapScroll;
        for (uint8_t row = 0U; row < 4U; ++row) {
            const uint8_t index = static_cast<uint8_t>(first + row);
            if (index >= lapCount) break;
            const int16_t y = static_cast<int16_t>(320 + row * 29);
            const uint32_t splitTicks = Stopwatch_GetLapTicks(index);
            char lapNumber[8];
            char split[12];
            char total[12];
            formatLapNumber(Stopwatch_GetLapNumber(index), lapNumber);
            formatStopwatchTicks(splitTicks, split);
            formatStopwatchTicks(Stopwatch_GetLapTotalTicks(index), total);
            fillRounded(45, y, 697, 25, 7,
                        index == 0U ? rgb(35, 55, 68) : rowSurface, dirty);
            fillRounded(50, y + 6, 3, 13, 1, index == 0U ? cyan : rgb(74, 89, 116), dirty);
            drawText(lapNumber, 62, y + 6, 2, white, dirty);
            const bool isFast = lapCount > 1U && splitTicks == fastest && fastest != slowest;
            const bool isSlow = lapCount > 1U && splitTicks == slowest && fastest != slowest;
            drawText(split, 240, y + 6, 2, isFast ? cyan : (isSlow ? amber : white), dirty);
            drawText(total, 488, y + 6, 2, white, dirty);
            if (isFast) drawText("FAST", 662, y + 9, 1, cyan, dirty);
            else if (isSlow) drawText("SLOW", 662, y + 9, 1, amber, dirty);
        }

        if (lapCount > 4U) {
            const int16_t trackY = 321;
            const int16_t trackH = 112;
            int16_t thumbH = static_cast<int16_t>((trackH * 4U) / lapCount);
            if (thumbH < 18) thumbH = 18;
            const int16_t thumbY = static_cast<int16_t>(trackY +
                (trackH - thumbH) * first / maxScroll);
            fillRounded(751, trackY, 5, trackH, 2, rgb(48, 59, 79), dirty);
            fillRounded(751, thumbY, 5, thumbH, 2, rgb(112, 132, 166), dirty);
        }
    }
}

void AppPanelWidget::drawSpectrum(const Rect& dirty) const
{
    AudioSpectrumSnapshot snapshot;
    AudioSpectrum_GetSnapshot(&snapshot);
    const colortype white = rgb(241, 247, 255);
    const colortype muted = rgb(137, 157, 187);
    const colortype grid = rgb(46, 66, 82);
    const colortype green = rgb(62, 218, 166);
    const colortype yellow = rgb(246, 194, 76);
    const colortype red = rgb(244, 91, 108);
    const colortype cyan = rgb(83, 211, 236);
    const colortype panel = rgb(16, 28, 37);
    const int16_t graphLeft = 68;
    const int16_t graphTop = 158;
    const int16_t graphRight = 518;
    const int16_t graphBottom = 389;
    const int16_t graphHeight = graphBottom - graphTop;

    drawText("AMPLITUDE (DBFS)", 31, 112, 1, muted, dirty);
    drawText("48 KHZ / 4-MIC / 1024 FFT", 305, 112, 1, muted, dirty);

    char peakText[40];
    if (snapshot.status == AUDIO_SPECTRUM_RUNNING && snapshot.generation > 2U) {
        char* cursor = appendText(peakText, "PEAK ");
        cursor = appendUnsigned(cursor, snapshot.peak_frequency_hz);
        cursor = appendText(cursor, " HZ  ");
        char db[12];
        formatSignedTenths(snapshot.peak_db_tenths, db);
        cursor = appendText(cursor, db);
        cursor = appendText(cursor, " DBFS");
        *cursor = 0;
    } else if (snapshot.status == AUDIO_SPECTRUM_STARTING) {
        std::strcpy(peakText, "STARTING MICROPHONES");
    } else if (snapshot.status == AUDIO_SPECTRUM_BUSY) {
        std::strcpy(peakText, "AUDIO RESOURCE BUSY");
    } else if (snapshot.status == AUDIO_SPECTRUM_ERROR) {
        std::strcpy(peakText, "DFSDM CAPTURE ERROR");
    } else {
        std::strcpy(peakText, "WAITING FOR AUDIO");
    }
    drawText(peakText, 31, 130, 1,
             snapshot.status == AUDIO_SPECTRUM_ERROR ? red : white, dirty);

    /* The live spectrum occupies the left two thirds. The compact five-pixel
       bar pitch preserves all 80 analysis bands without clipping 10 kHz. */
    fillRounded(25, 148, 500, 270, 12, panel, dirty);
    static const int16_t dbTicks[5] = {0, -20, -40, -60, -80};
    static const char* const dbLabels[5] = {"0", "-20", "-40", "-60", "-80"};
    for (uint8_t tick = 0U; tick < 5U; ++tick) {
        const int16_t y = static_cast<int16_t>(
            graphTop + (-dbTicks[tick] * graphHeight) / 90);
        fillLocal(Rect(graphLeft, y, graphRight - graphLeft, 1), dirty, grid);
        drawText(dbLabels[tick],
                 static_cast<int16_t>(61 - textLength(dbLabels[tick]) * 6),
                 y - 3, 1, muted, dirty);
    }
    fillLocal(Rect(graphLeft, graphTop, 2, graphHeight + 1), dirty, muted);
    fillLocal(Rect(graphLeft, graphBottom, graphRight - graphLeft, 2), dirty, muted);

    if (snapshot.status == AUDIO_SPECTRUM_RUNNING) {
        for (uint8_t band = 0U; band < AUDIO_SPECTRUM_BAND_COUNT; ++band) {
            int16_t db = snapshot.bands_db_tenths[band];
            if (db < -900) db = -900;
            if (db > 0) db = 0;
            const int16_t height = static_cast<int16_t>(
                ((db + 900) * (graphHeight - 4)) / 900);
            if (height <= 0) continue;
            const int16_t x = static_cast<int16_t>(
                76 + band * (graphRight - graphLeft - 16) /
                AUDIO_SPECTRUM_BAND_COUNT);
            const colortype color = db > -120 ? red : (db > -300 ? yellow : green);
            fillRounded(x, static_cast<int16_t>(graphBottom - height),
                        4, height, 1, color, dirty);
        }
    }

    /* Show exactly which narrow FFT window is feeding the acoustic trigger. */
    const int16_t targetX = static_cast<int16_t>(graphLeft +
        (snapshot.trigger_frequency_hz * (graphRight - graphLeft)) / 10000U);
    fillLocal(Rect(targetX, graphTop, 2, graphHeight), dirty, cyan);

    static const char* const frequencyLabels[6] =
        {"0", "2K", "4K", "6K", "8K", "10K"};
    for (uint8_t tick = 0U; tick < 6U; ++tick) {
        const int16_t x = static_cast<int16_t>(
            graphLeft + tick * (graphRight - graphLeft) / 5);
        fillLocal(Rect(x, graphBottom, 1, 5), dirty, muted);
        drawText(frequencyLabels[tick],
                 static_cast<int16_t>(x - textLength(frequencyLabels[tick]) * 3),
                 397, 1, muted, dirty);
    }
    drawText("FREQUENCY (HZ)", 222, 421, 1, muted, dirty);

    /* Right third: LSE/RTC-referenced acoustic stopwatch. */
    fillRounded(537, 108, 238, 310, 14, panel, dirty);
    drawText("TONE STOPWATCH", 565, 121, 2, white, dirty);

    char timeText[16];
    formatStopwatchTicks(snapshot.stopwatch_ticks, timeText);
    fillRounded(548, 148, 216, 62, 11, rgb(22, 40, 50), dirty);
    drawText(timeText,
             static_cast<int16_t>(656 - textLength(timeText) * 6),
             164, 2, snapshot.stopwatch_running != 0U ? cyan : white, dirty);
    char stopwatchState[24];
    char* stateCursor = appendText(stopwatchState,
        snapshot.stopwatch_running != 0U ? "RUNNING / " : "STOPPED / ");
    stateCursor = appendText(stateCursor,
        snapshot.trigger_armed != 0U ? "ARMED" : "WAIT QUIET");
    *stateCursor = 0;
    drawText(stopwatchState, 554, 194, 1,
             snapshot.stopwatch_running != 0U ? green : muted, dirty);
    drawText("LSE 1024HZ", 704, 194, 1, muted, dirty);

    char levelText[22];
    char* levelCursor = appendText(levelText, "TONE ");
    char levelDb[12];
    formatSignedTenths(snapshot.trigger_level_db_tenths, levelDb);
    levelCursor = appendText(levelCursor, levelDb);
    levelCursor = appendText(levelCursor, " DBFS");
    *levelCursor = 0;
    drawText(levelText, 548, 220, 1,
             snapshot.trigger_level_db_tenths >=
             snapshot.trigger_threshold_db_tenths ? yellow : muted, dirty);

    fillRounded(548, 239, 216, 10, 5, rgb(37, 55, 65), dirty);
    int16_t meterDb = snapshot.trigger_level_db_tenths;
    if (meterDb < -900) meterDb = -900;
    if (meterDb > 0) meterDb = 0;
    int16_t meterWidth = static_cast<int16_t>((meterDb + 900) * 216 / 900);
    if (meterWidth > 0) {
        fillRounded(548, 239, meterWidth, 10, 5,
                    meterDb >= snapshot.trigger_threshold_db_tenths ? yellow : green,
                    dirty);
    }
    const int16_t thresholdX = static_cast<int16_t>(548 +
        (snapshot.trigger_threshold_db_tenths + 900) * 216 / 900);
    fillLocal(Rect(thresholdX, 236, 2, 16), dirty, red);

    fillRounded(542, 265, 110, 76, 12, rgb(27, 50, 62), dirty);
    drawText("TARGET", 576, 276, 1, muted, dirty);
    char frequencyText[12];
    char* frequencyCursor = appendUnsigned(frequencyText,
                                           snapshot.trigger_frequency_hz / 1000U);
    frequencyCursor = appendText(frequencyCursor, " KHZ");
    *frequencyCursor = 0;
    drawText(frequencyText,
             static_cast<int16_t>(597 - textLength(frequencyText) * 6),
             299, 2, cyan, dirty);
    drawText("TAP TO CHANGE", 559, 326, 1, muted, dirty);

    fillRounded(660, 265, 110, 76, 12, rgb(46, 40, 38), dirty);
    drawText("TRIGGER", 692, 276, 1, muted, dirty);
    char thresholdText[12];
    formatSignedTenths(snapshot.trigger_threshold_db_tenths, thresholdText);
    drawText(thresholdText,
             static_cast<int16_t>(715 - textLength(thresholdText) * 6),
             299, 2, yellow, dirty);
    drawText("DBFS", 703, 326, 1, muted, dirty);

    fillRounded(542, 351, 228, 57, 12, rgb(44, 51, 65), dirty);
    drawText("RESET TIMER", 590, 368, 2, white, dirty);
    char triggerText[20];
    char* triggerCursor = appendText(triggerText, "TRIGGERS ");
    triggerCursor = appendUnsigned(triggerCursor, snapshot.trigger_count);
    *triggerCursor = 0;
    drawText(triggerText, 550, 395, 1, muted, dirty);
}

void AppPanelWidget::drawSettings(const Rect& dirty) const
{
    USB_MSC_Snapshot msc;
    USB_AudioSnapshot audio;
    USB_MSC_GetSnapshot(&msc);
    USB_Audio_GetSnapshot(&audio);

    const colortype white = rgb(244, 248, 255);
    const colortype muted = rgb(142, 159, 190);
    const colortype surface = rgb(19, 28, 44);
    const colortype cyan = rgb(65, 207, 226);
    const colortype green = rgb(59, 213, 154);
    const colortype amber = rgb(245, 181, 66);
    const colortype red = rgb(239, 83, 101);
    const colortype track = rgb(44, 55, 75);
    const int16_t offset = settingsScroll;
    const Rect contentDirty = clippedRect(
        dirty, Rect(20, SETTINGS_VIEW_TOP, 765,
                    SETTINGS_VIEW_BOTTOM - SETTINGS_VIEW_TOP));

    /* The app header remains fixed while these cards move under a clipped
       viewport. This avoids a large nested ScrollableContainer and keeps the
       existing procedural/DMA2D rendering path. */
    if (contentDirty.width > 0 && contentDirty.height > 0) {
        fillRounded(35, static_cast<int16_t>(118 - offset), 730, 91, 17,
                    surface, contentDirty);
        fillRounded(53, static_cast<int16_t>(136 - offset), 52, 52, 13,
                    rgb(40, 91, 117), contentDirty);
        drawText("SD", 65, static_cast<int16_t>(153 - offset), 2, cyan,
                 contentDirty);
        drawText("USB HS SD CARD READER", 122,
                 static_cast<int16_t>(132 - offset), 2, white, contentDirty);
        drawText("EJECT ON COMPUTER BEFORE SWITCHING OFF", 122,
                 static_cast<int16_t>(164 - offset), 1, amber, contentDirty);
        drawText(msc.card_present != 0U ? "SD MEDIA PRESENT" : "NO SD MEDIA",
                 122, static_cast<int16_t>(184 - offset), 1,
                 msc.card_present != 0U ? muted : amber, contentDirty);

        const bool mscOn = msc.requested != 0U;
        fillRounded(659, static_cast<int16_t>(143 - offset), 78, 38, 19,
                    mscOn ? green : rgb(62, 73, 94), contentDirty);
        fillRounded(mscOn ? 703 : 665,
                    static_cast<int16_t>(148 - offset), 28, 28, 14, white,
                    contentDirty);

        /* USB Audio card. Controls are spaced vertically so every row keeps
           a distinct touch target after scrolling. */
        fillRounded(35, static_cast<int16_t>(220 - offset), 730, 313, 17,
                    surface, contentDirty);
        fillRounded(53, static_cast<int16_t>(238 - offset), 52, 52, 13,
                    rgb(92, 58, 123), contentDirty);
        drawText("AU", 63, static_cast<int16_t>(255 - offset), 2,
                 rgb(194, 132, 255), contentDirty);
        drawText("USB AUDIO OUTPUT", 122,
                 static_cast<int16_t>(234 - offset), 2, white, contentDirty);
        drawText("44.1 / 48 KHZ / 16 BIT / ASYNC FB / SAI DMA", 122,
                 static_cast<int16_t>(260 - offset), 1, muted, contentDirty);

        const bool audioOn = audio.requested != 0U;
        fillRounded(659, static_cast<int16_t>(241 - offset), 78, 38, 19,
                    audioOn ? green : rgb(62, 73, 94), contentDirty);
        fillRounded(audioOn ? 703 : 665,
                    static_cast<int16_t>(246 - offset), 28, 28, 14, white,
                    contentDirty);

        const bool windowsHost = audio.host_mode == USB_AUDIO_HOST_WINDOWS;
        drawText("HOST", 122, static_cast<int16_t>(299 - offset), 1, muted,
                 contentDirty);
        fillRounded(220, static_cast<int16_t>(290 - offset), 174, 29, 9,
                    windowsHost ? track : rgb(40, 113, 132), contentDirty);
        fillRounded(402, static_cast<int16_t>(290 - offset), 174, 29, 9,
                    windowsHost ? rgb(65, 94, 145) : track, contentDirty);
        drawText("LINUX / UAC1", 244, static_cast<int16_t>(299 - offset), 1,
                 windowsHost ? muted : white, contentDirty);
        drawText("WINDOWS / UAC2", 420, static_cast<int16_t>(299 - offset), 1,
                 windowsHost ? white : muted, contentDirty);

        const bool spdifOutput = audio.output == USB_AUDIO_OUTPUT_SPDIF;
        drawText("OUTPUT", 122, static_cast<int16_t>(337 - offset), 1, muted,
                 contentDirty);
        fillRounded(220, static_cast<int16_t>(328 - offset), 174, 29, 9,
                    spdifOutput ? track : rgb(40, 113, 132), contentDirty);
        fillRounded(402, static_cast<int16_t>(328 - offset), 174, 29, 9,
                    spdifOutput ? rgb(92, 58, 123) : track, contentDirty);
        drawText("WM8994 LINE", 242, static_cast<int16_t>(337 - offset), 1,
                 spdifOutput ? muted : white, contentDirty);
        drawText("S/PDIF CN8", 430, static_cast<int16_t>(337 - offset), 1,
                 spdifOutput ? white : muted, contentDirty);

        if (spdifOutput) {
            const bool repeat =
                audio.spdif_mode == USB_AUDIO_SPDIF_REPEAT_4X;
            const bool exact =
                audio.spdif_mode == USB_AUDIO_SPDIF_UPSAMPLE_4X;
            const bool headroom =
                audio.spdif_mode == USB_AUDIO_SPDIF_UPSAMPLE_4X_HEADROOM;
            const bool tpdf =
                audio.spdif_mode == USB_AUDIO_SPDIF_UPSAMPLE_4X_TPDF;
            const bool iir =
                audio.spdif_mode == USB_AUDIO_SPDIF_UPSAMPLE_4X_IIR;
            const bool hybrid =
                audio.spdif_mode == USB_AUDIO_SPDIF_UPSAMPLE_4X_HYBRID;
            const bool hybridNs2 =
                audio.spdif_mode == USB_AUDIO_SPDIF_UPSAMPLE_4X_HYBRID_NS2;
            const bool butterworthNs2 =
                audio.spdif_mode ==
                    USB_AUDIO_SPDIF_UPSAMPLE_4X_BUTTERWORTH_NS2;
            const bool butterworthMinNs2 =
                audio.spdif_mode ==
                    USB_AUDIO_SPDIF_UPSAMPLE_4X_BUTTERWORTH_MINPHASE_NS2;
            const bool besselMinNs2 =
                audio.spdif_mode ==
                    USB_AUDIO_SPDIF_UPSAMPLE_4X_BESSEL_MINPHASE_NS2;
            const bool besselOpenNs2 =
                audio.spdif_mode ==
                    USB_AUDIO_SPDIF_UPSAMPLE_4X_BESSEL_OPEN_NS2;
            const bool besselMinNs5 =
                audio.spdif_mode ==
                    USB_AUDIO_SPDIF_UPSAMPLE_4X_BESSEL_MINPHASE_NS5;
            const bool firMinNs5 =
                audio.spdif_mode ==
                    USB_AUDIO_SPDIF_UPSAMPLE_4X_FIR_MINPHASE_NS5;
            drawText("S/PDIF MODE", 122,
                     static_cast<int16_t>(378 - offset), 1, muted,
                     contentDirty);
            drawText("FILTERS", 122,
                     static_cast<int16_t>(411 - offset), 1, muted,
                     contentDirty);
            const int16_t modeY1 = static_cast<int16_t>(367 - offset);
            const int16_t modeY2 = static_cast<int16_t>(400 - offset);
            const int16_t modeY3 = static_cast<int16_t>(433 - offset);
            fillRounded(220, modeY1, 96, 29, 8,
                        (repeat || exact || headroom || tpdf || iir || hybrid ||
                         hybridNs2 || butterworthNs2 || butterworthMinNs2 ||
                         besselMinNs2 || besselOpenNs2 || besselMinNs5 ||
                         firMinNs5) ?
                            track : rgb(40, 113, 132), contentDirty);
            fillRounded(320, modeY1, 96, 29, 8,
                        repeat ? rgb(35, 125, 103) : track, contentDirty);
            fillRounded(420, modeY1, 96, 29, 8,
                        exact ? rgb(92, 58, 123) : track, contentDirty);
            fillRounded(520, modeY1, 96, 29, 8,
                        headroom ? rgb(160, 105, 38) : track, contentDirty);
            fillRounded(620, modeY1, 96, 29, 8,
                        tpdf ? rgb(153, 62, 128) : track, contentDirty);
            fillRounded(220, modeY2, 59, 29, 8,
                        iir ? rgb(46, 105, 154) : track, contentDirty);
            fillRounded(283, modeY2, 59, 29, 8,
                        hybrid ? rgb(45, 126, 142) : track, contentDirty);
            fillRounded(346, modeY2, 59, 29, 8,
                        hybridNs2 ? rgb(42, 145, 114) : track, contentDirty);
            fillRounded(409, modeY2, 59, 29, 8,
                        butterworthNs2 ? rgb(113, 91, 170) : track,
                        contentDirty);
            fillRounded(472, modeY2, 59, 29, 8,
                        butterworthMinNs2 ? rgb(169, 92, 70) : track,
                        contentDirty);
            fillRounded(535, modeY2, 59, 29, 8,
                        besselMinNs2 ? rgb(54, 137, 155) : track,
                        contentDirty);
            fillRounded(598, modeY2, 59, 29, 8,
                        besselOpenNs2 ? rgb(53, 157, 132) : track,
                        contentDirty);
            fillRounded(661, modeY2, 59, 29, 8,
                        besselMinNs5 ? rgb(143, 91, 174) : track,
                        contentDirty);
            fillRounded(220, modeY3, 118, 29, 8,
                        firMinNs5 ? rgb(184, 112, 52) : track,
                        contentDirty);
            drawText("NATIVE", 250, static_cast<int16_t>(376 - offset), 1,
                     (repeat || exact || headroom || tpdf || iir || hybrid ||
                      hybridNs2 || butterworthNs2 || butterworthMinNs2 ||
                      besselMinNs2 || besselOpenNs2 || besselMinNs5 ||
                      firMinNs5) ?
                         muted : white, contentDirty);
            drawText("4X HOLD", 347, static_cast<int16_t>(376 - offset), 1,
                     repeat ? white : muted, contentDirty);
            drawText("4X EXACT", 444, static_cast<int16_t>(376 - offset), 1,
                     exact ? white : muted, contentDirty);
            drawText("4X -1DB", 547, static_cast<int16_t>(376 - offset), 1,
                     headroom ? white : muted, contentDirty);
            drawText("4X TPDF", 647, static_cast<int16_t>(376 - offset), 1,
                     tpdf ? white : muted, contentDirty);
            drawText("4X IIR", 231, static_cast<int16_t>(409 - offset), 1,
                     iir ? white : muted, contentDirty);
            drawText("HYBRID", 294, static_cast<int16_t>(409 - offset), 1,
                     hybrid ? white : muted, contentDirty);
            drawText("HYB NS2", 355, static_cast<int16_t>(409 - offset), 1,
                     hybridNs2 ? white : muted, contentDirty);
            drawText("BTR NS2", 418, static_cast<int16_t>(409 - offset), 1,
                     butterworthNs2 ? white : muted, contentDirty);
            drawText("BTR MIN", 481, static_cast<int16_t>(409 - offset), 1,
                     butterworthMinNs2 ? white : muted, contentDirty);
            drawText("BES MIN", 544, static_cast<int16_t>(409 - offset), 1,
                     besselMinNs2 ? white : muted, contentDirty);
            drawText("BES OPEN", 604, static_cast<int16_t>(409 - offset), 1,
                     besselOpenNs2 ? white : muted, contentDirty);
            drawText("BES NS5", 670, static_cast<int16_t>(409 - offset), 1,
                     besselMinNs5 ? white : muted, contentDirty);
            drawText("FIR MIN", 258, static_cast<int16_t>(442 - offset), 1,
                     firMinNs5 ? white : muted, contentDirty);
            drawText("KAISER 90DB / POLYPHASE / NS5", 356,
                     static_cast<int16_t>(442 - offset), 1, muted,
                     contentDirty);
        } else {
            char gainText[5];
            formatPercent(audio.volume, 100U, gainText);
            drawText("HW GAIN", 122, static_cast<int16_t>(378 - offset), 1,
                     muted, contentDirty);
            fillRounded(220, static_cast<int16_t>(374 - offset), 390, 14, 7,
                        track, contentDirty);
            const int16_t gainWidth = static_cast<int16_t>(
                (static_cast<uint32_t>(audio.volume) * 390U) / 100U);
            if (gainWidth > 0) {
                fillRounded(220, static_cast<int16_t>(374 - offset),
                            gainWidth, 14, 7, cyan, contentDirty);
            }
            const int16_t knobX = static_cast<int16_t>(220 + gainWidth);
            fillRounded(static_cast<int16_t>(knobX - 9),
                        static_cast<int16_t>(371 - offset), 18, 20, 9,
                        white, contentDirty);
            drawText(gainText, 630, static_cast<int16_t>(372 - offset), 2,
                     white, contentDirty);
        }

        const char* statusText = "DISABLED";
        colortype statusColor = muted;
        if (audio.requested != 0U || audio.status != USB_AUDIO_OFF) {
            if (audio.status == USB_AUDIO_STARTING) {
                statusText = spdifOutput ? "PREPARING S/PDIF OUTPUT" :
                                          "PREPARING WM8994 LINE OUT";
                statusColor = amber;
            } else if (audio.status == USB_AUDIO_BUSY) {
                statusText = "WAITING FOR MUSIC RECORDER OR SPECTRUM";
                statusColor = amber;
            } else if (audio.status == USB_AUDIO_ACTIVE &&
                       audio.streaming != 0U) {
                statusText = "PLAYING FROM COMPUTER";
                statusColor = green;
            } else if (audio.status == USB_AUDIO_ACTIVE &&
                       audio.configured != 0U) {
                statusText = "CONNECTED - WAITING FOR AUDIO";
                statusColor = green;
            } else if (audio.status == USB_AUDIO_ACTIVE) {
                statusText = "CONNECT USB HS CABLE";
                statusColor = cyan;
            } else if (audio.status == USB_AUDIO_STOPPING) {
                statusText = "STOPPING USB AUDIO";
                statusColor = amber;
            } else if (audio.status == USB_AUDIO_ERROR) {
                statusText = "USB AUDIO ERROR - RETRY";
                statusColor = red;
            }
        } else if (msc.active != 0U && msc.card_present == 0U) {
            statusText = "USB ENABLED - INSERT SD CARD";
            statusColor = amber;
        } else if (msc.status == USB_MSC_STARTING) {
            statusText = "PREPARING SD CARD";
            statusColor = amber;
        } else if (msc.status == USB_MSC_ACTIVE && msc.configured != 0U) {
            statusText = "SD READER CONNECTED";
            statusColor = green;
        } else if (msc.status == USB_MSC_ACTIVE) {
            statusText = "CONNECT USB HS CABLE";
            statusColor = cyan;
        } else if (msc.status == USB_MSC_NO_CARD) {
            statusText = "INSERT SD CARD";
            statusColor = amber;
        } else if (msc.status == USB_MSC_AUDIO_BUSY) {
            statusText = "WAITING FOR MUSIC OR RECORDER";
            statusColor = amber;
        } else if (msc.status == USB_MSC_ERROR) {
            statusText = "USB STORAGE ERROR - RETRY";
            statusColor = red;
        }
        fillRounded(55, static_cast<int16_t>(480 - offset), 690, 34, 11,
                    rgb(14, 23, 37), contentDirty);
        fillRounded(72, static_cast<int16_t>(491 - offset), 12, 12, 6,
                    statusColor, contentDirty);
        drawText(statusText, 98, static_cast<int16_t>(487 - offset), 2,
                 statusColor, contentDirty);
        const bool usbConfigured =
            audio.configured != 0U || msc.configured != 0U;
        const bool usbHighSpeed = audio.configured != 0U ?
            audio.high_speed != 0U : msc.high_speed != 0U;
        if (usbConfigured) {
            drawText(usbHighSpeed ? "HIGH SPEED" : "FULL SPEED",
                     646, static_cast<int16_t>(492 - offset), 1,
                     usbHighSpeed ? green : amber, contentDirty);
        }

        char audioStats[58];
        char xrunStats[24];
        char* cursor;
        if (audio.requested != 0U || audio.active != 0U) {
            cursor = appendUnsigned(audioStats, audio.sample_rate);
            if (spdifOutput &&
                audio.spdif_mode != USB_AUDIO_SPDIF_NATIVE) {
                cursor = appendText(cursor, ">");
                cursor = appendUnsigned(cursor, audio.sample_rate * 4U);
            }
            cursor = appendText(cursor, " HZ / USB PACKETS ");
            cursor = appendUnsigned(cursor, audio.received_packets);
            *cursor = 0;
            cursor = appendText(xrunStats, "XRUN U");
            cursor = appendUnsigned(cursor, audio.underruns);
            cursor = appendText(cursor, " / O");
            cursor = appendUnsigned(cursor, audio.overruns);
            *cursor = 0;
        } else {
            cursor = appendText(audioStats, "READ ");
            cursor = appendUnsigned(cursor, msc.read_blocks);
            cursor = appendText(cursor, " BLOCKS");
            *cursor = 0;
            cursor = appendText(xrunStats, "WRITE ");
            cursor = appendUnsigned(cursor, msc.written_blocks);
            cursor = appendText(cursor, " BLOCKS");
            *cursor = 0;
        }
        drawText(audioStats, 58, static_cast<int16_t>(532 - offset), 1,
                 white, contentDirty);
        drawText(xrunStats, 570, static_cast<int16_t>(532 - offset), 1,
                 (audio.underruns != 0U || audio.overruns != 0U) ?
                     amber : white, contentDirty);

        /* Start-buffer card. The setting is deliberately below the main USB
           controls so it can use a wide, finger-friendly slider. */
        fillRounded(35, static_cast<int16_t>(578 - offset), 730, 143, 17,
                    surface, contentDirty);
        fillRounded(53, static_cast<int16_t>(596 - offset), 52, 52, 13,
                    rgb(38, 103, 91), contentDirty);
        drawText("BF", 63, static_cast<int16_t>(613 - offset), 2, green,
                 contentDirty);
        drawText("USB START BUFFER", 122,
                 static_cast<int16_t>(592 - offset), 2, white, contentDirty);
        drawText("MORE FRAMES = MORE START LATENCY AND GAP HEADROOM", 122,
                 static_cast<int16_t>(618 - offset), 1, muted, contentDirty);

        char bufferValue[28];
        cursor = appendUnsigned(bufferValue, audio.start_frames);
        cursor = appendText(cursor, " FR / ");
        const uint32_t latencyRate =
            audio.sample_rate != 0U ? audio.sample_rate : 48000U;
        cursor = appendUnsigned(cursor,
            static_cast<uint32_t>(
                (static_cast<uint64_t>(audio.start_frames) * 1000U) /
                latencyRate));
        cursor = appendText(cursor, " MS");
        *cursor = 0;
        drawText(bufferValue,
                 static_cast<int16_t>(735 - textLength(bufferValue) * 6),
                 static_cast<int16_t>(598 - offset), 1, green, contentDirty);

        const int16_t sliderY = static_cast<int16_t>(651 - offset);
        fillRounded(SETTINGS_BUFFER_SLIDER_X, sliderY,
                    SETTINGS_BUFFER_SLIDER_WIDTH, 14, 7, track, contentDirty);
        const int16_t knobX = settingsStartFramesToX(audio.start_frames);
        const int16_t activeWidth =
            static_cast<int16_t>(knobX - SETTINGS_BUFFER_SLIDER_X);
        if (activeWidth > 0) {
            fillRounded(SETTINGS_BUFFER_SLIDER_X, sliderY, activeWidth, 14, 7,
                        green, contentDirty);
        }
        fillRounded(static_cast<int16_t>(knobX - 11),
                    static_cast<int16_t>(sliderY - 5), 22, 24, 11, white,
                    contentDirty);
        drawText("6144", SETTINGS_BUFFER_SLIDER_X,
                 static_cast<int16_t>(674 - offset), 1, muted, contentDirty);
        drawText("30720",
                 static_cast<int16_t>(SETTINGS_BUFFER_SLIDER_X +
                                      SETTINGS_BUFFER_SLIDER_WIDTH - 36),
                 static_cast<int16_t>(674 - offset), 1, muted, contentDirty);
        drawText(audio.streaming != 0U ?
                 "SAVED - APPLIES AT NEXT START OR REBUFFER" :
                 "ACTIVE FOR THE NEXT DMA START",
                 220, static_cast<int16_t>(699 - offset), 1,
                 audio.streaming != 0U ? amber : green, contentDirty);
    }

    /* Fixed scroll affordance remains visible while the content moves. */
    drawText("SCROLL", 660, 94, 1, muted, dirty);
    fillRounded(778, 118, 5, 322, 2, rgb(43, 57, 78), dirty);
    const int16_t thumbHeight = 204;
    const int16_t thumbTravel = static_cast<int16_t>(322 - thumbHeight);
    const int16_t thumbY = static_cast<int16_t>(118 +
        (static_cast<int32_t>(settingsScroll) * thumbTravel) /
        SETTINGS_SCROLL_MAX);
    fillRounded(778, thumbY, 5, thumbHeight, 2, cyan, dirty);
}

void AppPanelWidget::drawCalculator(const Rect& dirty) const
{
    const colortype white = rgb(247, 249, 255);
    const colortype muted = rgb(146, 158, 184);
    const colortype displaySurface = rgb(20, 25, 37);
    const colortype numberSurface = rgb(42, 49, 66);
    const colortype utilitySurface = rgb(86, 96, 116);
    const colortype operationSurface = rgb(242, 151, 45);
    const colortype activeOperation = rgb(255, 191, 96);
    const colortype pressedGlow = rgb(255, 235, 178);
    const colortype pressedSurface = rgb(112, 128, 158);

    /* A dedicated left display keeps the value readable without competing
       with the keypad. The right keypad uses the full available height so
       every touch target is substantially taller. */
    fillRounded(30, 120, 285, 307, 20, displaySurface, dirty);
    drawText("CURRENT VALUE", 50, 145, 1, muted, dirty);
    fillRounded(50, 169, 54, 4, 2, operationSurface, dirty);

    if (calculatorHistory[0] != 0) {
        const uint8_t historyLength = textLength(calculatorHistory);
        const uint8_t visibleLength = historyLength > 38U ? 38U : historyLength;
        const char* visibleHistory = calculatorHistory + historyLength - visibleLength;
        drawText("EXPRESSION", 50, 275, 1, muted, dirty);
        drawText(visibleHistory,
                 static_cast<int16_t>(290 - visibleLength * 6),
                 298, 1, white, dirty);
    } else {
        drawText("EXPRESSION", 50, 275, 1, muted, dirty);
        drawText("READY", 50, 298, 1, white, dirty);
    }

    uint8_t valueScale = 5U;
    if (calculatorLength > 8U) valueScale = 4U;
    if (calculatorLength > 11U) valueScale = 3U;
    if (calculatorLength > 15U) valueScale = 2U;
    int16_t valueX = static_cast<int16_t>(290 - calculatorLength * 6 * valueScale);
    if (valueX < 45) valueX = 45;
    drawText(calculatorInput, valueX,
             static_cast<int16_t>(215 - valueScale * 4U),
             valueScale, calculatorError ? rgb(255, 103, 112) : white, dirty);

    fillRounded(50, 340, 245, 62, 14, rgb(31, 38, 53), dirty);
    drawText(calculatorError ? "CHECK INPUT" :
             (calculatorOperation == 0 ? "READY" : "OPERATION PENDING"),
             67, 354, 1, calculatorError ? rgb(255, 103, 112) : muted, dirty);
    if (calculatorOperation != 0) {
        char operationText[2] = {calculatorOperation, 0};
        drawText(operationText, 258, 354, 3, operationSurface, dirty);
    } else {
        drawText("C TO CLEAR", 67, 377, 1, muted, dirty);
    }

    static const char* const labels[4][4] = {
        {"C", "+/-", "DEL", "/"},
        {"7", "8", "9", "*"},
        {"4", "5", "6", "-"},
        {"1", "2", "3", "+"}
    };
    static const char operationKeys[4] = {'/', '*', '-', '+'};
    for (uint8_t row = 0U; row < 4U; ++row) {
        for (uint8_t col = 0U; col < 4U; ++col) {
            const int16_t x = static_cast<int16_t>(340 + col * 109);
            const int16_t y = static_cast<int16_t>(120 + row * 63);
            colortype surface = numberSurface;
            if (row == 0U && col < 3U) surface = utilitySurface;
            if (col == 3U) {
                surface = calculatorOperation == operationKeys[row] ?
                          activeOperation : operationSurface;
            }
            const char key = row == 0U ?
                (col == 0U ? 'C' : (col == 1U ? 'S' : (col == 2U ? 'B' : '/'))) :
                (row == 1U ? (col == 0U ? '7' : (col == 1U ? '8' : (col == 2U ? '9' : '*'))) :
                 (row == 2U ? (col == 0U ? '4' : (col == 1U ? '5' : (col == 2U ? '6' : '-'))) :
                              (col == 0U ? '1' : (col == 1U ? '2' : (col == 2U ? '3' : '+')))));
            if (calculatorPressedKey == key) {
                fillRounded(x - 4, y - 4, 110, 63, 17, pressedGlow, dirty);
                surface = col == 3U ? rgb(255, 183, 76) : pressedSurface;
            }
            fillRounded(x, y, 102, 55, 13, surface, dirty);
            const char* buttonLabel = labels[row][col];
            const uint8_t length = textLength(buttonLabel);
            drawText(buttonLabel,
                     static_cast<int16_t>(x + (102 - length * 18) / 2),
                     y + 17, 3, white, dirty);
        }
    }

    if (calculatorPressedKey == '0')
        fillRounded(336, 368, 219, 63, 17, pressedGlow, dirty);
    fillRounded(340, 372, 211, 55, 13,
                calculatorPressedKey == '0' ? pressedSurface : numberSurface, dirty);
    drawText("0", 437, 389, 3, white, dirty);
    if (calculatorPressedKey == '.')
        fillRounded(554, 368, 110, 63, 17, pressedGlow, dirty);
    fillRounded(558, 372, 102, 55, 13,
                calculatorPressedKey == '.' ? pressedSurface : numberSurface, dirty);
    drawText(".", 600, 389, 3, white, dirty);
    if (calculatorPressedKey == '=')
        fillRounded(663, 368, 110, 63, 17, pressedGlow, dirty);
    fillRounded(667, 372, 102, 55, 13,
                calculatorPressedKey == '=' ? rgb(255, 183, 76) : operationSurface, dirty);
    drawText("=", 709, 389, 3, white, dirty);
}

void AppPanelWidget::drawCards(const Rect& dirty) const
{
    const colortype card = rgb(static_cast<uint8_t>(accentR / 4 + 27),
                               static_cast<uint8_t>(accentG / 4 + 30),
                               static_cast<uint8_t>(accentB / 4 + 40));
    const colortype white = rgb(244, 248, 255);
    switch (appIndex % 4) {
    case 0:
        for (uint8_t row = 0; row < 2; ++row) {
            for (uint8_t col = 0; col < 4; ++col) {
                const int16_t x = 32 + col * 188;
                const int16_t y = 145 + row * 132;
                fillRounded(x, y, 166, 110, 14,
                            rgb(static_cast<uint8_t>(accentR / 2 + col * 12),
                                static_cast<uint8_t>(accentG / 2 + row * 24),
                                static_cast<uint8_t>(accentB / 2 + 35)), dirty);
                fillRounded(x + 14, y + 16, 42, 42, 21, white, dirty);
                fillLocal(Rect(x + 15, y + 76, 111, 5), dirty, white);
                fillLocal(Rect(x + 15, y + 89, 75, 4), dirty, rgb(186, 202, 229));
            }
        }
        break;
    case 1:
        fillRounded(45, 145, 250, 250, 26, card, dirty);
        fillRounded(76, 176, 188, 188, 94, rgb(accentR, accentG, accentB), dirty);
        drawText("NOW PLAYING", 350, 167, 2, rgb(171, 190, 225), dirty);
        drawText(label, 350, 211, 4, white, dirty);
        fillLocal(Rect(350, 288, 360, 5), dirty, rgb(90, 108, 147));
        fillLocal(Rect(350, 288, 220, 5), dirty, white);
        fillRounded(433, 329, 60, 60, 30, white, dirty);
        for (int16_t row = 0; row < 24; ++row) fillLocal(Rect(457, 347 + row / 2, 2 + row / 2, 1), dirty, card);
        drawText("02:18", 350, 307, 2, rgb(176, 194, 226), dirty);
        drawText("04:06", 652, 307, 2, rgb(176, 194, 226), dirty);
        break;
    case 2:
        for (uint8_t i = 0; i < 4; ++i) {
            const int16_t y = 145 + i * 68;
            fillRounded(35, y, 730, 54, 12, card, dirty);
            fillRounded(51, y + 11, 32, 32, 9, rgb(accentR, accentG, accentB), dirty);
            drawText(i == 0 ? "RECENT" : (i == 1 ? "FAVORITES" : (i == 2 ? "SHARED" : "ARCHIVE")),
                     103, y + 18, 2, white, dirty);
            fillLocal(Rect(680, y + 25, 45, 3), dirty, rgb(151, 170, 208));
        }
        break;
    default:
        for (uint8_t i = 0; i < 3; ++i) {
            const int16_t x = 35 + i * 247;
            fillRounded(x, 145, 220, 106, 15, card, dirty);
            drawText(i == 0 ? "TODAY" : (i == 1 ? "ACTIVE" : "TOTAL"), x + 18, 165, 2, rgb(170, 191, 226), dirty);
            drawText(i == 0 ? "24" : (i == 1 ? "08" : "128"), x + 18, 199, 4, white, dirty);
        }
        fillRounded(35, 274, 714, 122, 15, card, dirty);
        drawText("ACTIVITY", 55, 294, 2, rgb(170, 191, 226), dirty);
        for (uint8_t i = 0; i < 11; ++i) {
            const int16_t h = 18 + ((i * 23) % 60);
            fillRounded(61 + i * 58, 379 - h, 22, h, 6, rgb(accentR, accentG, accentB), dirty);
        }
        break;
    }
}

void AppPanelWidget::drawSystemUsage(const Rect& dirty) const
{
    const uint32_t ramTotal = 512U * 1024U;
    const uint32_t romTotal = 2048U * 1024U;
    const uint32_t externalRamTotal = 16U * 1024U * 1024U;
    const uint32_t externalRomTotal = 64U * 1024U * 1024U;
    /* Banks 0 and 1 are intentionally kept exclusive to the two framebuffers;
       report reserved SDRAM rather than only the visible pixel payload. */
    const uint32_t externalRamUsed =
        (2U * LCD_SDRAM_BANK_SIZE) + USB_AUDIO_INPUT_RING_BYTES;
    /* No section or TouchGFX asset is currently linked into QSPI. */
    const uint32_t externalRomUsed = 0U;
    uint32_t ramUsed;
    uint32_t romUsed;
    uint32_t heapFree;
    uint32_t heapMinimum;
    getSystemMemoryUsage(ramUsed, romUsed, heapFree, heapMinimum);

    const colortype white = rgb(244, 248, 255);
    const colortype muted = rgb(145, 164, 199);
    const colortype card = rgb(20, 29, 45);
    const colortype cardBorder = rgb(45, 60, 84);
    const colortype ramColor = rgb(61, 211, 177);
    const colortype romColor = rgb(73, 153, 247);
    char heapLine[24];

    drawText("4 MEMORY BANKS", 611, 91, 1, muted, dirty);
    formatHeapValue("HEAP FREE ", heapFree, heapLine);
    (void)heapMinimum;

    const auto drawMemoryCard = [this, &dirty, white, muted, card, cardBorder](
        int16_t x, int16_t y, const char* title, const char* subtitle,
        const char* detail, uint32_t used, uint32_t total, colortype color) {
        char percent[5];
        char usage[24];
        fillRounded(x, y, 350, 134, 16, cardBorder, dirty);
        fillRounded(x + 4, y + 4, 342, 126, 14, card, dirty);
        fillRounded(x + 18, y + 17, 4, 29, 2, color, dirty);
        drawText(title, x + 34, y + 16, 2, white, dirty);
        drawText(subtitle, x + 34, y + 38, 1, muted, dirty);
        formatPercent(used, total, percent);
        drawText(percent,
                 static_cast<int16_t>(x + 315 - textLength(percent) * 12),
                 y + 18, 4, color, dirty);
        fillRounded(x + 24, y + 57, 302, 14, 7, rgb(46, 59, 78), dirty);
        int16_t bar = static_cast<int16_t>(
            (static_cast<uint64_t>(used) * 302U) / total);
        if (bar < 5 && used != 0U) bar = 5;
        fillRounded(x + 24, y + 57, bar, 14, 7, color, dirty);
        formatUsage(used, total, usage);
        drawText(usage,
                 static_cast<int16_t>(x + 175 - textLength(usage) * 6),
                 y + 82, 2, white, dirty);
        drawText(detail, x + 24, y + 111, 1, muted, dirty);
    };

    drawMemoryCard(35, 130, "INT RAM", "MCU SRAM - LIVE", heapLine,
                   ramUsed, ramTotal, ramColor);
    drawMemoryCard(415, 130, "INT ROM", "MCU FLASH - IMAGE", "LINKED FIRMWARE",
                   romUsed, romTotal, romColor);
    drawMemoryCard(35, 282, "EXT RAM", "FMC SDRAM - 16 MB", "2 X 4 MB FB BANKS + 2 MB RING",
                   externalRamUsed, externalRamTotal, rgb(244, 174, 71));
    drawMemoryCard(415, 282, "EXT ROM", "QSPI FLASH - 64 MB", "NO QSPI ASSETS",
                   externalRomUsed, externalRomTotal, rgb(174, 103, 245));
}

void AppPanelWidget::drawMusicList(const Rect& dirty) const
{
    SD_StorageSnapshot storage;
    AudioPlayerSnapshot player;
    SD_Storage_GetSnapshot(&storage);
    AudioPlayer_GetSnapshot(&player);

    const colortype white = rgb(244, 248, 255);
    const colortype muted = rgb(145, 164, 199);
    const colortype surface = rgb(20, 28, 48);
    const colortype border = rgb(48, 55, 91);
    const colortype purple = rgb(157, 92, 240);
    const colortype cyan = rgb(58, 208, 190);
    const uint8_t wavCount = wavEntryCount(storage);
    char count[20];

    fillRounded(35, 112, 730, 58, 16, border, dirty);
    fillRounded(39, 116, 722, 50, 13, surface, dirty);
    fillRounded(54, 132, 10, 10, 5,
                storage.status == SD_STORAGE_READY ? cyan : rgb(244, 174, 71), dirty);
    drawText(storage.status == SD_STORAGE_READY ? "WAV LIBRARY" : "SD CARD UNAVAILABLE",
             77, 129, 2, white, dirty);

    if (storage.status != SD_STORAGE_READY) {
        fillRounded(35, 187, 730, 240, 18, surface, dirty);
        const char* storageHint = storage.status == SD_STORAGE_NO_CARD ?
            "INSERT SD CARD" : (storage.status == SD_STORAGE_USB_EXPORTED ?
            "SD CARD IS CONNECTED TO USB" : "WAITING FOR STORAGE");
        drawText(storageHint,
                 static_cast<int16_t>(400 - textLength(storageHint) * 6),
                 292, 2, muted, dirty);
        return;
    }

    char* cursor = appendUnsigned(count, wavCount);
    cursor = appendText(cursor, wavCount == 1U ? " WAV FILE" : " WAV FILES");
    *cursor = 0;
    drawText(count, static_cast<int16_t>(742 - textLength(count) * 6), 133, 1, muted, dirty);

    if (wavCount == 0U) {
        fillRounded(35, 187, 730, 240, 18, surface, dirty);
        drawText("NO WAV FILES FOUND", 286, 284, 2, muted, dirty);
        drawText("RECORD A FILE OR COPY PCM WAV TO SD", 268, 318, 1, muted, dirty);
        return;
    }

    const uint8_t visibleRows = 6U;
    const uint8_t maxScroll = wavCount > visibleRows ?
        static_cast<uint8_t>(wavCount - visibleRows) : 0U;
    const uint8_t first = musicScroll > maxScroll ? maxScroll : musicScroll;
    for (uint8_t row = 0U; row < visibleRows; ++row) {
        const SD_StorageEntry* entry = wavEntryAt(storage, static_cast<uint8_t>(first + row));
        if (entry == 0) break;
        const int16_t y = static_cast<int16_t>(181 + row * 41);
        const bool current = player.filename[0] != 0 &&
                             strcmp(player.filename, entry->name) == 0;
        char size[16];
        fillRounded(35, y, 710, 35, 10,
                    current ? rgb(39, 37, 70) : surface, dirty);
        fillRounded(48, y + 6, 41, 23, 7, current ? cyan : purple, dirty);
        drawText("WAV", 58, y + 14, 1, rgb(255, 255, 255), dirty);
        drawText(entry->name, 105, y + 9, 2, white, dirty);
        formatFileSize(entry->size, size);
        drawText(size, static_cast<int16_t>(720 - textLength(size) * 6),
                 y + 13, 1, muted, dirty);
        if (current && (player.status == AUDIO_PLAYER_PLAYING ||
                        player.status == AUDIO_PLAYER_PAUSED)) {
            drawText(player.status == AUDIO_PLAYER_PLAYING ? "PLAYING" : "PAUSED",
                     552, y + 13, 1, cyan, dirty);
        }
    }

    if (wavCount > visibleRows) {
        const int16_t trackY = 181;
        const int16_t trackH = 240;
        int16_t thumbH = static_cast<int16_t>(
            (static_cast<uint32_t>(trackH) * visibleRows) / wavCount);
        if (thumbH < 20) thumbH = 20;
        const int16_t thumbY = static_cast<int16_t>(trackY +
            (trackH - thumbH) * first / maxScroll);
        fillRounded(754, trackY, 5, trackH, 2, rgb(43, 57, 78), dirty);
        fillRounded(754, thumbY, 5, thumbH, 2, purple, dirty);
    }
}

void AppPanelWidget::drawMusicPlayer(const Rect& dirty) const
{
    AudioPlayerSnapshot player;
    AudioPlayer_GetSnapshot(&player);

    const colortype white = rgb(246, 249, 255);
    const colortype muted = rgb(145, 163, 195);
    const colortype purple = rgb(157, 92, 240);
    const colortype cyan = rgb(55, 211, 177);
    const colortype red = rgb(240, 86, 104);
    char position[6];
    char duration[6];
    char volume[6];
    char metadata[32];

    fillRounded(35, 104, 105, 38, 12, rgb(54, 63, 88), dirty);
    drawText("BACK", 63, 117, 2, white, dirty);

    fillRounded(35, 155, 220, 254, 22, rgb(50, 38, 78), dirty);
    fillRounded(43, 163, 204, 238, 18, rgb(30, 24, 53), dirty);
    fillRounded(76, 191, 138, 138, 69, purple, dirty);
    fillRounded(105, 220, 80, 80, 40, rgb(224, 211, 252), dirty);
    fillLocal(Rect(142, 221, 8, 55), dirty, rgb(74, 52, 111));
    fillLocal(Rect(148, 221, 28, 7), dirty, rgb(74, 52, 111));
    fillRounded(126, 269, 24, 18, 9, rgb(74, 52, 111), dirty);
    drawText(USB_Audio_GetOutput() == USB_AUDIO_OUTPUT_SPDIF ?
             "S/PDIF" : "WM8994", 105, 355, 2, white, dirty);
    drawText("SAI1  DMA", 113, 382, 1, muted, dirty);

    drawText(player.filename[0] != 0 ? player.filename : "SELECTING WAV",
             292, 151, 3, white, dirty);

    char* cursor = appendUnsigned(metadata, player.sample_rate / 1000U);
    cursor = appendText(cursor, " KHZ  /  ");
    cursor = appendUnsigned(cursor, player.bits_per_sample);
    cursor = appendText(cursor, " BIT  /  ");
    cursor = appendText(cursor, player.channels == 1U ? "MONO" : "STEREO");
    *cursor = 0;
    drawText(metadata, 294, 190, 1, muted, dirty);

    const char* state = "READY";
    colortype stateColor = muted;
    if (player.status == AUDIO_PLAYER_LOADING) { state = "LOADING"; stateColor = purple; }
    else if (player.status == AUDIO_PLAYER_PLAYING) { state = "PLAYING"; stateColor = cyan; }
    else if (player.status == AUDIO_PLAYER_PAUSED) { state = "PAUSED"; stateColor = purple; }
    else if (player.status == AUDIO_PLAYER_FINISHED) { state = "FINISHED"; stateColor = cyan; }
    else if (player.status == AUDIO_PLAYER_UNSUPPORTED) { state = "UNSUPPORTED PCM WAV"; stateColor = red; }
    else if (player.status == AUDIO_PLAYER_NO_CARD) { state = "SD CARD REMOVED"; stateColor = red; }
    else if (player.status == AUDIO_PLAYER_BUSY) { state = "AUDIO RESOURCE BUSY"; stateColor = red; }
    else if (player.status == AUDIO_PLAYER_ERROR) { state = "PLAYBACK ERROR"; stateColor = red; }
    drawText(state, 294, 213, 1, stateColor, dirty);

    fillRounded(294, 250, 438, 12, 6, rgb(48, 58, 79), dirty);
    int16_t progress = player.data_bytes == 0U ? 0 : static_cast<int16_t>(
        (static_cast<uint64_t>(player.position_bytes) * 438U) / player.data_bytes);
    if (progress > 438) progress = 438;
    fillRounded(294, 250, progress, 12, 6, purple, dirty);
    fillRounded(static_cast<int16_t>(294 + progress - 7), 246, 14, 20, 7, white, dirty);
    formatPlayerTime(player.position_ms, position);
    formatPlayerTime(player.duration_ms, duration);
    drawText(position, 294, 273, 1, muted, dirty);
    drawText(duration, 702, 273, 1, muted, dirty);

    const bool canToggle = player.status == AUDIO_PLAYER_PLAYING ||
                           player.status == AUDIO_PLAYER_PAUSED ||
                           player.status == AUDIO_PLAYER_FINISHED;
    fillRounded(455, 298, 86, 70, 28, canToggle ? purple : rgb(62, 67, 88), dirty);
    if (player.status == AUDIO_PLAYER_PLAYING) {
        fillRounded(481, 316, 9, 35, 3, white, dirty);
        fillRounded(506, 316, 9, 35, 3, white, dirty);
    } else {
        for (int16_t row = 0; row < 34; ++row) {
            fillLocal(Rect(483, static_cast<int16_t>(316 + row),
                           static_cast<int16_t>(5 + row / 2), 1), dirty, white);
        }
    }

    drawText("VOLUME", 294, 387, 1, muted, dirty);
    formatPercent(player.volume, 100U, volume);
    drawText(volume, static_cast<int16_t>(732 - textLength(volume) * 12),
             378, 2, white, dirty);
    fillRounded(360, 405, 332, 10, 5, rgb(48, 58, 79), dirty);
    int16_t volumeWidth = static_cast<int16_t>((player.volume * 332U) / 100U);
    fillRounded(360, 405, volumeWidth, 10, 5, cyan, dirty);
    fillRounded(static_cast<int16_t>(360 + volumeWidth - 7), 400, 14, 20, 7, white, dirty);
}

void AppPanelWidget::drawMusic(const Rect& dirty) const
{
    if (musicDetail != 0U) drawMusicPlayer(dirty);
    else drawMusicList(dirty);
}

void AppPanelWidget::drawFiles(const Rect& dirty) const
{
    SD_StorageSnapshot snapshot;
    SD_Storage_GetSnapshot(&snapshot);

    const colortype white = rgb(244, 248, 255);
    const colortype muted = rgb(145, 164, 199);
    const colortype surface = rgb(20, 31, 50);
    const colortype border = rgb(43, 62, 91);
    const colortype blue = rgb(48, 156, 246);
    const colortype green = rgb(55, 211, 162);
    const colortype amber = rgb(245, 184, 62);
    char summary[32];
    char freeText[20];
    char percent[6];
    char count[20];
    char range[24];

    fillRounded(35, 112, 730, 91, 16, border, dirty);
    fillRounded(39, 116, 722, 83, 13, surface, dirty);

    const char* statusText = "NO SD CARD";
    colortype statusColor = muted;
    if (snapshot.status == SD_STORAGE_MOUNTING) {
        statusText = "MOUNTING SD CARD";
        statusColor = amber;
    } else if (snapshot.status == SD_STORAGE_READY) {
        statusText = "SD CARD READY";
        statusColor = green;
    } else if (snapshot.status == SD_STORAGE_NO_FILESYSTEM) {
        statusText = "FORMAT CARD AS FAT32";
        statusColor = amber;
    } else if (snapshot.status == SD_STORAGE_ERROR) {
        statusText = "SD CARD ERROR";
        statusColor = rgb(244, 99, 112);
    }
    fillRounded(53, 132, 10, 10, 5, statusColor, dirty);
    drawText(statusText, 76, 129, 2, white, dirty);

    if (snapshot.status == SD_STORAGE_READY) {
        const uint32_t usedKb = snapshot.total_kb >= snapshot.free_kb ?
            snapshot.total_kb - snapshot.free_kb : 0U;
        formatStorageSummary(snapshot.free_kb, snapshot.total_kb, summary);
        formatStorageFree(snapshot.free_kb, freeText);
        formatStoragePercent(usedKb, snapshot.total_kb, percent);
        drawText(percent, static_cast<int16_t>(738 - textLength(percent) * 12),
                 126, 2, green, dirty);
        drawText(summary, 53, 158, 1, white, dirty);
        drawText(freeText, static_cast<int16_t>(742 - textLength(freeText) * 6),
                 158, 1, muted, dirty);

        fillRounded(53, 181, 689, 9, 4, rgb(43, 57, 78), dirty);
        int16_t usedBar = snapshot.total_kb == 0U ? 0 : static_cast<int16_t>(
            (static_cast<uint64_t>(usedKb) * 689U) / snapshot.total_kb);
        if (usedBar < 4 && usedKb != 0U) usedBar = 4;
        if (usedBar > 689) usedBar = 689;
        fillRounded(53, 181, usedBar, 9, 4, green, dirty);

        if (snapshot.total_entry_count == 0U) {
            fillRounded(35, 216, 730, 211, 18, surface, dirty);
            drawText("SD CARD IS EMPTY", 292, 300, 2, muted, dirty);
        } else {
            char* cursor = appendUnsigned(count, snapshot.total_entry_count);
            cursor = appendText(cursor, snapshot.total_entry_count == 1U ? " ITEM" : " ITEMS");
            *cursor = 0;
            drawText(count, 39, 208, 1, muted, dirty);

            const uint8_t visibleRows = 6U;
            const uint8_t maxScroll = snapshot.entry_count > visibleRows ?
                static_cast<uint8_t>(snapshot.entry_count - visibleRows) : 0U;
            const uint8_t first = fileScroll > maxScroll ? maxScroll : fileScroll;
            uint8_t end = static_cast<uint8_t>(first + visibleRows);
            if (end > snapshot.entry_count) end = snapshot.entry_count;

            cursor = appendUnsigned(range, static_cast<uint32_t>(first + 1U));
            *cursor++ = '-';
            cursor = appendUnsigned(cursor, end);
            cursor = appendText(cursor, " OF ");
            cursor = appendUnsigned(cursor, snapshot.total_entry_count);
            *cursor = 0;
            drawText(range, static_cast<int16_t>(742 - textLength(range) * 6),
                     208, 1, muted, dirty);

            for (uint8_t row = 0U; row < visibleRows; ++row) {
                const uint8_t index = static_cast<uint8_t>(first + row);
                if (index >= snapshot.entry_count) break;
                const SD_StorageEntry& entry = snapshot.entries[index];
                const int16_t y = static_cast<int16_t>(225 + row * 34);
                char size[16];
                fillRounded(35, y, 710, 29, 8, surface, dirty);
                fillRounded(49, y + 6, 18, 17, 4,
                            entry.is_directory != 0U ? amber : blue, dirty);
                if (entry.is_directory != 0U) {
                    fillLocal(Rect(52, y + 5, 9, 4), dirty, amber);
                    formatFileSize(0U, size);
                    size[0] = 'F'; size[1] = 'O'; size[2] = 'L';
                    size[3] = 'D'; size[4] = 'E'; size[5] = 'R'; size[6] = 0;
                } else {
                    formatFileSize(entry.size, size);
                }
                drawText(entry.name, 82, y + 8, 2, white, dirty);
                drawText(size,
                         static_cast<int16_t>(724 - textLength(size) * 6),
                         y + 10, 1, muted, dirty);
            }

            if (snapshot.entry_count > visibleRows) {
                const int16_t trackY = 225;
                const int16_t trackH = 199;
                int16_t thumbH = static_cast<int16_t>(
                    (static_cast<uint32_t>(trackH) * visibleRows) / snapshot.entry_count);
                if (thumbH < 18) thumbH = 18;
                const int16_t thumbY = static_cast<int16_t>(trackY +
                    (trackH - thumbH) * first / maxScroll);
                fillRounded(754, trackY, 5, trackH, 2, rgb(43, 57, 78), dirty);
                fillRounded(754, thumbY, 5, thumbH, 2, blue, dirty);
            }
        }
    } else {
        fillRounded(35, 216, 730, 211, 18, surface, dirty);
        const char* hint = snapshot.status == SD_STORAGE_NO_CARD ?
            "INSERT A FAT32 SD CARD" :
            (snapshot.status == SD_STORAGE_MOUNTING ? "READING CARD" :
             (snapshot.status == SD_STORAGE_NO_FILESYSTEM ? "FAT OR FAT32 REQUIRED" :
              (snapshot.status == SD_STORAGE_USB_EXPORTED ?
               "SD CARD IS SHARED BY USB" : "REMOVE AND REINSERT CARD")));
        drawText(hint,
                 static_cast<int16_t>(400 - textLength(hint) * 6),
                 300, 2, muted, dirty);
    }
}

void AppPanelWidget::drawRecorder(const Rect& dirty) const
{
    AudioRecorderSnapshot snapshot;
    AudioRecorder_GetSnapshot(&snapshot);

    const colortype white = rgb(245, 249, 255);
    const colortype muted = rgb(143, 162, 194);
    const colortype surface = rgb(18, 27, 43);
    const colortype border = rgb(46, 59, 82);
    const colortype red = rgb(239, 77, 91);
    const colortype cyan = rgb(55, 211, 177);
    const colortype amber = rgb(245, 181, 66);
    const bool recording = snapshot.status == AUDIO_RECORDER_RECORDING;
    const bool transition = snapshot.status == AUDIO_RECORDER_STARTING ||
                            snapshot.status == AUDIO_RECORDER_STOPPING;
    const bool usbOwnsStorage = SD_Storage_ApplicationAccessAllowed() == 0U;
    char elapsed[9];
    char size[16];
    formatRecorderTime(snapshot.elapsed_seconds, elapsed);
    formatFileSize(snapshot.data_bytes, size);

    fillRounded(35, 116, 730, 88, 18, border, dirty);
    fillRounded(39, 120, 722, 80, 15, surface, dirty);
    fillRounded(55, 137, 12, 12, 6,
                recording ? red : (snapshot.codec_ready ? cyan : amber), dirty);
    drawText(recording ? "RECORDING - 4 MICROPHONES" : "DFSDM 4-MIC ARRAY",
             81, 134, 2, white, dirty);
    drawText(snapshot.codec_ready ? "WM8994 CODEC READY" : "WM8994 CODEC NOT FOUND",
             81, 166, 1, snapshot.codec_ready ? cyan : amber, dirty);
    drawText("16 KHZ  /  16-BIT  /  MONO WAV", 505, 146, 1, muted, dirty);

    fillRounded(35, 216, 730, 112, 18, rgb(13, 21, 35), dirty);
    drawText(elapsed, 60, 241, 5, white, dirty);
    drawText(snapshot.filename[0] != 0 ? snapshot.filename : "NEXT FILE AUTO NUMBERED",
             60, 299, 1, muted, dirty);
    drawText(size, static_cast<int16_t>(740 - textLength(size) * 6), 299, 1, muted, dirty);

    const uint32_t level = static_cast<uint32_t>(snapshot.peak) * 100U / 32767U;
    for (uint8_t i = 0U; i < 28U; ++i) {
        uint32_t shaped = (level * (35U + ((i * 29U) % 66U))) / 100U;
        if (shaped > 100U) shaped = 100U;
        int16_t height = static_cast<int16_t>(4U + shaped * 64U / 100U);
        fillRounded(static_cast<int16_t>(405 + i * 11),
                    static_cast<int16_t>(280 - height / 2), 6, height, 3,
                    recording ? (shaped > 78U ? red : cyan) : rgb(54, 70, 94), dirty);
    }

    colortype buttonColor = recording ? red : cyan;
    const char* buttonText = recording ? "STOP RECORDING" : "START RECORDING";
    if (usbOwnsStorage && !recording && !transition) {
        buttonColor = rgb(70, 79, 98);
        buttonText = "USB OWNS SD CARD";
    } else if (snapshot.status == AUDIO_RECORDER_STARTING) {
        buttonColor = amber;
        buttonText = "STARTING";
    } else if (snapshot.status == AUDIO_RECORDER_STOPPING) {
        buttonColor = amber;
        buttonText = "SAVING WAV";
    }
    fillRounded(245, 345, 310, 62, 18, transition ? rgb(93, 77, 50) : buttonColor, dirty);
    drawText(buttonText,
             static_cast<int16_t>(400 - textLength(buttonText) * 9),
             366, 3, white, dirty);

    const char* status = "READY - TAP TO RECORD";
    colortype statusColor = muted;
    if (usbOwnsStorage && !recording && !transition) {
        status = "DISABLE USB SD READER IN SETTINGS";
        statusColor = amber;
    } else if (recording) {
        status = "WRITING WAV TO SD CARD";
        statusColor = red;
    } else if (snapshot.status == AUDIO_RECORDER_NO_CARD) {
        status = "INSERT A FAT32 SD CARD";
        statusColor = amber;
    } else if (snapshot.status == AUDIO_RECORDER_FILESYSTEM_ERROR) {
        status = "SD CARD WRITE ERROR";
        statusColor = red;
    } else if (snapshot.status == AUDIO_RECORDER_AUDIO_ERROR) {
        status = "MICROPHONE DMA ERROR";
        statusColor = red;
    } else if (transition) {
        status = "PLEASE WAIT";
        statusColor = amber;
    }
    drawText(status, static_cast<int16_t>(400 - textLength(status) * 6),
             420, 2, statusColor, dirty);
}

void AppPanelWidget::draw(const Rect& dirty) const
{
    const bool insideStopwatchTimeCard = appIndex == 4U &&
        dirty.x >= 225 && dirty.right() <= 575 &&
        dirty.y >= 135 && dirty.bottom() <= 177;
    if (!insideStopwatchTimeCard && dirty.x <= 0 && dirty.y <= 0 &&
        dirty.right() >= 800 && dirty.bottom() >= 480) {
        TouchGFX_BeginFullScreenRedraw();
    }
    if (!insideStopwatchTimeCard) {
        const int16_t firstBand = static_cast<int16_t>((dirty.y / 8) * 8);
        for (int16_t y = firstBand; y < dirty.bottom(); y += 8) {
            const int16_t sampleY = static_cast<int16_t>(y + 4);
            const uint8_t r = static_cast<uint8_t>(12 + accentR * (480 - sampleY) / 2400);
            const uint8_t g = static_cast<uint8_t>(17 + accentG * (480 - sampleY) / 2600);
            const uint8_t b = static_cast<uint8_t>(31 + accentB * (480 - sampleY) / 1900);
            fillLocal(Rect(0, y, 800, 8), dirty, rgb(r, g, b));
        }
    }
    fillLocal(Rect(0, 0, 800, 58), dirty, rgb(10, 15, 29));
    drawText("11:42", 22, 18, 2, rgb(235, 242, 255), dirty);
    drawText(label, 28, 78, 4, rgb(247, 250, 255), dirty);
    fillRounded(735, 8, 52, 44, 13, rgb(62, 72, 99), dirty);
    drawCloseX(761, 30, rgb(251, 252, 255), dirty);
    if (appIndex == 1U) drawMusic(dirty);
    else if (appIndex == 2U) drawFiles(dirty);
    else if (appIndex == 4U) drawStopwatch(dirty);
    else if (appIndex == 6U) drawCalculator(dirty);
    else if (appIndex == 10U) drawSpectrum(dirty);
    else if (appIndex == 11U) drawSettings(dirty);
    else if (appIndex == 13U) drawRecorder(dirty);
    else if (appIndex == 15U) drawSystemUsage(dirty);
    else drawCards(dirty);
    drawText("TOUCHGFX RGB888", 28, 450, 2, rgb(132, 153, 194), dirty);
}

TrackingTouchArea::TrackingTouchArea() : owner(0) {}

void TrackingTouchArea::setOwner(MainView* view) { owner = view; }

void TrackingTouchArea::handleClickEvent(const ClickEvent& event)
{
    TouchArea::handleClickEvent(event);
    if (owner) owner->handleTrackingClick(event);
}

void TrackingTouchArea::handleDragEvent(const DragEvent& event)
{
    TouchArea::handleDragEvent(event);
    if (owner) owner->handleTrackingDrag(event);
}

MainView::MainView()
    : appVisible(false), dragging(false), animating(false), currentPage(0), selectedApp(0xFF),
      pressX(0), pressY(0), dragStartOffset(0), stripOffset(0), targetOffset(0),
      stopwatchLapScroll(0), stopwatchLapScrollStart(0),
      fileScroll(0), fileScrollStart(0),
      musicScroll(0), musicScrollStart(0), musicWavCount(0), musicDetail(false),
      settingsScroll(0), settingsScrollStart(0),
      lastStopwatchRenderSlot(0xFFFFFFFFUL),
      lastStopwatchControlGeneration(0xFFFFFFFFUL),
      lastStorageGeneration(0xFFFFFFFFUL),
      lastMusicGeneration(0xFFFFFFFFUL), lastMusicStorageGeneration(0xFFFFFFFFUL),
      lastMusicRenderSlot(0xFFFFFFFFUL),
      lastSystemRenderSecond(0xFFFFFFFFUL), lastRecorderGeneration(0xFFFFFFFFUL),
      lastSpectrumGeneration(0xFFFFFFFFUL), lastSpectrumRenderSlot(0xFFFFFFFFUL),
      lastUsbMscGeneration(0xFFFFFFFFUL), lastUsbAudioGeneration(0xFFFFFFFFUL)
{
    desktopBackground.setPosition(0, 0, 800, 480);
    pageStrip.setPosition(0, 0, 1600, 480);
    page0.setPosition(0, 0, 800, 480);
    page1.setPosition(800, 0, 800, 480);

    for (uint8_t i = 0; i < 16; ++i) {
        icons[i].configure(i, APP_NAMES[i], APP_COLORS[i][0], APP_COLORS[i][1], APP_COLORS[i][2]);
        icons[i].setPosition(ICON_X[i % 4], ICON_Y[(i % 8) / 4], 104, 112);
        if (i < 8) page0.add(icons[i]);
        else page1.add(icons[i]);
    }
    pageStrip.add(page0);
    pageStrip.add(page1);

    appPanel.setPosition(0, 0, 800, 480);
    appPanel.setVisible(false);
    touchArea.setPosition(0, 0, 800, 480);
    touchArea.setOwner(this);
}

void MainView::setupScreen()
{
    add(desktopBackground);
    add(pageStrip);
    add(appPanel);
    add(touchArea);
}

void MainView::tearDownScreen() {}

void MainView::setStripOffset(int16_t offset)
{
    if (offset < -800) offset = -800;
    if (offset > 0) offset = 0;
    if (stripOffset == offset) return;
    stripOffset = offset;
    pageStrip.setX(offset);

    /* A moving transparent container exposes pixels at both its old and new
       positions. With two alternating buffers, invalidating only the icon
       strip leaves two-frame-old background fragments in the back buffer.
       Rebuild the complete desktop frame for motion frames; static screens
       still retain normal TouchGFX partial invalidation. */
    desktopBackground.invalidate();
    pageStrip.invalidate();
}

void MainView::settleToPage(uint8_t page)
{
    currentPage = page > 0 ? 1 : 0;
    targetOffset = static_cast<int16_t>(-800 * currentPage);
    animating = true;
    desktop_current_page = currentPage;
}

void MainView::handleTickEvent()
{
    if (appVisible) {
        if (selectedApp == 1U) {
            bool refresh = false;
            const uint32_t playerGeneration = audio_player_generation;
            const uint32_t storageGeneration = sd_storage_generation;
            if (playerGeneration != lastMusicGeneration) {
                lastMusicGeneration = playerGeneration;
                refresh = true;
            }
            if (storageGeneration != lastMusicStorageGeneration) {
                SD_StorageSnapshot storage;
                SD_Storage_GetSnapshot(&storage);
                const uint8_t wavCount = wavEntryCount(storage);
                musicWavCount = wavCount;
                const uint8_t maxScroll = wavCount > 6U ?
                    static_cast<uint8_t>(wavCount - 6U) : 0U;
                if (musicScroll > maxScroll) {
                    musicScroll = maxScroll;
                    appPanel.setMusicScroll(musicScroll);
                }
                lastMusicStorageGeneration = storageGeneration;
                refresh = true;
            }
            if (musicDetail) {
                const uint32_t renderSlot = HAL_GetTick() / 100U;
                if (renderSlot != lastMusicRenderSlot) {
                    lastMusicRenderSlot = renderSlot;
                    refresh = true;
                }
            }
            if (refresh) appPanel.invalidateMusic();
        } else if (selectedApp == 2U) {
            const uint32_t generation = sd_storage_generation;
            if (generation != lastStorageGeneration) {
                lastStorageGeneration = generation;
                const uint8_t entryCount = SD_Storage_GetCachedEntryCount();
                const uint8_t maxScroll = entryCount > 6U ?
                    static_cast<uint8_t>(entryCount - 6U) : 0U;
                if (fileScroll > maxScroll) {
                    fileScroll = maxScroll;
                    appPanel.setFileScroll(fileScroll);
                }
                appPanel.invalidateFiles();
            }
        } else if (selectedApp == 4U) {
            const uint32_t controlGeneration = stopwatch_control_generation;
            if (controlGeneration != lastStopwatchControlGeneration) {
                lastStopwatchControlGeneration = controlGeneration;
                appPanel.invalidateStopwatchStartButton();
                appPanel.invalidateStopwatchTime();
            }
            /* The LSE timer runs at 1024 Hz, but the visual counter is capped at
               25 FPS so rendering cannot starve touch sampling. */
            const uint32_t renderSlot = static_cast<uint32_t>(
                (static_cast<uint64_t>(Stopwatch_GetTicks()) * 25U) /
                RTC_LSE_TICK_HZ);
            if (renderSlot != lastStopwatchRenderSlot) {
                lastStopwatchRenderSlot = renderSlot;
                appPanel.invalidateStopwatchTime();
            }
        } else if (selectedApp == 10U) {
            AudioSpectrumSnapshot spectrum;
            AudioSpectrum_GetSnapshot(&spectrum);
            const uint32_t generation = spectrum.generation;
            const uint32_t renderSlot = HAL_GetTick() / 50U;
            if ((generation != lastSpectrumGeneration) &&
                (renderSlot != lastSpectrumRenderSlot)) {
                lastSpectrumGeneration = generation;
                lastSpectrumRenderSlot = renderSlot;
                appPanel.invalidateSpectrum();
            }
        } else if (selectedApp == 11U) {
            USB_MSC_Snapshot msc;
            USB_AudioSnapshot audio;
            USB_MSC_GetSnapshot(&msc);
            USB_Audio_GetSnapshot(&audio);
            if (msc.generation != lastUsbMscGeneration ||
                audio.generation != lastUsbAudioGeneration) {
                lastUsbMscGeneration = msc.generation;
                lastUsbAudioGeneration = audio.generation;
                appPanel.invalidateSettings();
            }
        } else if (selectedApp == 13U) {
            AudioRecorderSnapshot snapshot;
            AudioRecorder_GetSnapshot(&snapshot);
            if (snapshot.generation != lastRecorderGeneration) {
                lastRecorderGeneration = snapshot.generation;
                appPanel.invalidateRecorder();
            }
        } else if (selectedApp == 15U) {
            const uint32_t second = HAL_GetTick() / 1000U;
            if (second != lastSystemRenderSecond) {
                lastSystemRenderSecond = second;
                appPanel.invalidateSystemUsage();
            }
        }
        return;
    }
    if (!animating) return;
    const int16_t delta = static_cast<int16_t>(targetOffset - stripOffset);
    if (delta >= -2 && delta <= 2) {
        setStripOffset(targetOffset);
        animating = false;
        desktopBackground.setPage(currentPage);
        return;
    }
    int16_t step = static_cast<int16_t>(delta / 4);
    if (step == 0) step = delta > 0 ? 1 : -1;
    setStripOffset(static_cast<int16_t>(stripOffset + step));
}

void MainView::handleTrackingClick(const ClickEvent& event)
{
    const int16_t x = event.getX();
    const int16_t y = event.getY();
    if (event.getType() == ClickEvent::PRESSED) {
        pressX = x;
        pressY = y;
        dragging = false;
        dragStartOffset = stripOffset;
        stopwatchLapScrollStart = stopwatchLapScroll;
        fileScrollStart = fileScroll;
        musicScrollStart = musicScroll;
        settingsScrollStart = settingsScroll;
        animating = false;
        if (appVisible && selectedApp == 6U) {
            appPanel.setCalculatorPressedKey(calculatorKeyAt(x, y));
        }
        return;
    }
    if (event.getType() != ClickEvent::RELEASED) return;

    if (appVisible) {
        if (selectedApp == 1U) {
            if (x >= 715 && y <= 75 && !dragging) {
                closeApp();
                return;
            }

            if (musicDetail) {
                if (pressY >= 235 && pressY <= 282) {
                    int16_t sliderX = x;
                    if (sliderX < 294) sliderX = 294;
                    if (sliderX > 732) sliderX = 732;
                    AudioPlayer_RequestSeek(static_cast<uint16_t>(
                        (sliderX - 294) * 1000 / 438));
                    appPanel.invalidateMusic();
                    return;
                }
                if (pressY >= 385 && pressY <= 432) {
                    int16_t sliderX = x;
                    if (sliderX < 360) sliderX = 360;
                    if (sliderX > 692) sliderX = 692;
                    AudioPlayer_RequestVolume(static_cast<uint8_t>(
                        (sliderX - 360) * 100 / 332));
                    appPanel.invalidateMusic();
                    return;
                }
                if (dragging) return;
                if (pressX >= 25 && pressX <= 155 && pressY >= 95 && pressY <= 150) {
                    musicDetail = false;
                    appPanel.setMusicDetail(0U);
                } else if (pressX >= 435 && pressX <= 560 &&
                           pressY >= 288 && pressY <= 378) {
                    AudioPlayerSnapshot player;
                    AudioPlayer_GetSnapshot(&player);
                    if (player.status == AUDIO_PLAYER_PLAYING ||
                        player.status == AUDIO_PLAYER_PAUSED) {
                        AudioPlayer_RequestTogglePause();
                    } else if (player.filename[0] != 0) {
                        AudioPlayer_RequestPlay(player.filename);
                    }
                }
                return;
            }

            if (dragging) return;
            if (pressY >= 181 && pressY < 427) {
                const uint8_t row = static_cast<uint8_t>((pressY - 181) / 41);
                if (((pressY - 181) % 41) < 35) openMusicFileAtRow(row);
            }
            return;
        }

        if (selectedApp == 11U &&
            pressY >= SETTINGS_VIEW_TOP &&
            pressY <= SETTINGS_VIEW_BOTTOM) {
            USB_AudioSnapshot audio;
            USB_Audio_GetSnapshot(&audio);
            const int16_t contentPressY =
                static_cast<int16_t>(pressY + settingsScrollStart);
            if (contentPressY >= 360 && contentPressY <= 402 &&
                audio.output == USB_AUDIO_OUTPUT_WM8994) {
                USB_Audio_RequestVolume(settingsGainFromX(x));
                appPanel.invalidateSettingsGain();
                return;
            }
            if (contentPressY >= 636 && contentPressY <= 684) {
                USB_Audio_RequestStartFrames(settingsStartFramesFromX(x));
                appPanel.invalidateSettingsStartBuffer();
                return;
            }
        }

        if (dragging) return;
        if (x >= 715 && y <= 75) {
            closeApp();
        } else if (selectedApp == 6U) {
            const char pressedKey = calculatorKeyAt(pressX, pressY);
            const char releasedKey = calculatorKeyAt(x, y);
            appPanel.setCalculatorPressedKey(0);
            if (pressedKey != 0 && pressedKey == releasedKey) {
                appPanel.calculatorPress(pressedKey);
            }
        } else if (selectedApp == 4U && y >= 210 && y <= 274 &&
                   pressY >= 210 && pressY <= 274) {
            if (x >= 25 && x <= 260 && pressX >= 25 && pressX <= 260) {
                Stopwatch_Toggle();
                lastStopwatchControlGeneration = stopwatch_control_generation;
                appPanel.invalidateStopwatchStartButton();
            } else if (x >= 282 && x <= 517 && pressX >= 282 && pressX <= 517) {
                if (Stopwatch_Lap() != 0U) {
                    stopwatchLapScroll = 0U;
                    appPanel.setStopwatchLapScroll(0U);
                    appPanel.invalidateStopwatchLaps();
                }
            } else if (x >= 540 && x <= 775 && pressX >= 540 && pressX <= 775) {
                Stopwatch_Reset();
                lastStopwatchControlGeneration = stopwatch_control_generation;
                stopwatchLapScroll = 0U;
                appPanel.setStopwatchLapScroll(0U);
                appPanel.invalidateStopwatchBody();
            }
            lastStopwatchRenderSlot = static_cast<uint32_t>(
                (static_cast<uint64_t>(Stopwatch_GetTicks()) * 25U) /
                RTC_LSE_TICK_HZ);
        } else if (selectedApp == 10U) {
            AudioSpectrumSnapshot spectrum;
            AudioSpectrum_GetSnapshot(&spectrum);
            if (pressX >= 542 && pressX <= 652 && x >= 542 && x <= 652 &&
                pressY >= 265 && pressY <= 341 && y >= 265 && y <= 341) {
                uint32_t nextFrequency = 3000U;
                if (spectrum.trigger_frequency_hz == 3000U) nextFrequency = 4000U;
                else if (spectrum.trigger_frequency_hz == 4000U) nextFrequency = 5000U;
                else if (spectrum.trigger_frequency_hz == 5000U) nextFrequency = 6000U;
                else if (spectrum.trigger_frequency_hz == 6000U) nextFrequency = 7000U;
                else if (spectrum.trigger_frequency_hz == 7000U) nextFrequency = 8000U;
                else if (spectrum.trigger_frequency_hz == 8000U) nextFrequency = 9000U;
                AudioSpectrum_SetTriggerFrequency(nextFrequency);
                appPanel.invalidateSpectrum();
            } else if (pressX >= 660 && pressX <= 770 && x >= 660 && x <= 770 &&
                       pressY >= 265 && pressY <= 341 && y >= 265 && y <= 341) {
                int16_t nextThreshold = -200;
                if (spectrum.trigger_threshold_db_tenths == -200) nextThreshold = -300;
                else if (spectrum.trigger_threshold_db_tenths == -300) nextThreshold = -400;
                else if (spectrum.trigger_threshold_db_tenths == -400) nextThreshold = -500;
                else if (spectrum.trigger_threshold_db_tenths == -500) nextThreshold = -600;
                AudioSpectrum_SetTriggerThreshold(nextThreshold);
                appPanel.invalidateSpectrum();
            } else if (pressX >= 542 && pressX <= 770 && x >= 542 && x <= 770 &&
                       pressY >= 351 && pressY <= 408 && y >= 351 && y <= 408) {
                AudioSpectrum_ResetTriggerStopwatch();
                appPanel.invalidateSpectrum();
            }
        } else if (selectedApp == 11U) {
            const int16_t contentPressY =
                static_cast<int16_t>(pressY + settingsScroll);
            const int16_t contentReleaseY =
                static_cast<int16_t>(y + settingsScroll);
            if (pressX >= 620 && pressX <= 755 &&
                x >= 620 && x <= 755 &&
                contentPressY >= 118 && contentPressY <= 209 &&
                contentReleaseY >= 118 && contentReleaseY <= 209) {
                USB_MSC_Snapshot msc;
                USB_MSC_GetSnapshot(&msc);
                USB_MSC_RequestEnable(msc.requested == 0U ? 1U : 0U);
                appPanel.invalidateSettings();
            } else if (pressX >= 620 && pressX <= 755 &&
                       x >= 620 && x <= 755 &&
                       contentPressY >= 220 && contentPressY <= 282 &&
                       contentReleaseY >= 220 && contentReleaseY <= 282) {
                USB_AudioSnapshot audio;
                USB_Audio_GetSnapshot(&audio);
                USB_Audio_RequestEnable(audio.requested == 0U ? 1U : 0U);
                appPanel.invalidateSettings();
            } else if (contentPressY >= 286 && contentPressY <= 322 &&
                       contentReleaseY >= 286 &&
                       contentReleaseY <= 322) {
                if (pressX >= 210 && pressX <= 398 &&
                    x >= 210 && x <= 398) {
                    USB_Audio_RequestHostMode(USB_AUDIO_HOST_LINUX);
                    appPanel.invalidateSettings();
                } else if (pressX >= 398 && pressX <= 586 &&
                           x >= 398 && x <= 586) {
                    USB_Audio_RequestHostMode(USB_AUDIO_HOST_WINDOWS);
                    appPanel.invalidateSettings();
                }
            } else if (contentPressY >= 324 && contentPressY <= 360 &&
                       contentReleaseY >= 324 &&
                       contentReleaseY <= 360) {
                if (pressX >= 210 && pressX <= 398 &&
                    x >= 210 && x <= 398) {
                    USB_Audio_RequestOutput(USB_AUDIO_OUTPUT_WM8994);
                    appPanel.invalidateSettings();
                } else if (pressX >= 398 && pressX <= 586 &&
                           x >= 398 && x <= 586) {
                    USB_Audio_RequestOutput(USB_AUDIO_OUTPUT_SPDIF);
                    appPanel.invalidateSettings();
                }
            } else if (contentPressY >= 364 && contentPressY <= 398 &&
                       contentReleaseY >= 364 &&
                       contentReleaseY <= 398) {
                if (pressX >= 220 && pressX <= 318 &&
                    x >= 220 && x <= 318) {
                    USB_Audio_RequestSpdifMode(USB_AUDIO_SPDIF_NATIVE);
                    appPanel.invalidateSettings();
                } else if (pressX >= 319 && pressX <= 418 &&
                           x >= 319 && x <= 418) {
                    USB_Audio_RequestSpdifMode(USB_AUDIO_SPDIF_REPEAT_4X);
                    appPanel.invalidateSettings();
                } else if (pressX >= 419 && pressX <= 518 &&
                           x >= 419 && x <= 518) {
                    USB_Audio_RequestSpdifMode(
                        USB_AUDIO_SPDIF_UPSAMPLE_4X);
                    appPanel.invalidateSettings();
                } else if (pressX >= 519 && pressX <= 618 &&
                           x >= 519 && x <= 618) {
                    USB_Audio_RequestSpdifMode(
                        USB_AUDIO_SPDIF_UPSAMPLE_4X_HEADROOM);
                    appPanel.invalidateSettings();
                } else if (pressX >= 619 && pressX <= 720 &&
                           x >= 619 && x <= 720) {
                    USB_Audio_RequestSpdifMode(
                        USB_AUDIO_SPDIF_UPSAMPLE_4X_TPDF);
                    appPanel.invalidateSettings();
                }
            } else if (contentPressY >= 399 && contentPressY <= 431 &&
                       contentReleaseY >= 399 &&
                       contentReleaseY <= 431) {
                if (pressX >= 220 && pressX <= 281 &&
                    x >= 220 && x <= 281) {
                    USB_Audio_RequestSpdifMode(
                        USB_AUDIO_SPDIF_UPSAMPLE_4X_IIR);
                    appPanel.invalidateSettings();
                } else if (pressX >= 282 && pressX <= 344 &&
                           x >= 282 && x <= 344) {
                    USB_Audio_RequestSpdifMode(
                        USB_AUDIO_SPDIF_UPSAMPLE_4X_HYBRID);
                    appPanel.invalidateSettings();
                } else if (pressX >= 345 && pressX <= 407 &&
                           x >= 345 && x <= 407) {
                    USB_Audio_RequestSpdifMode(
                        USB_AUDIO_SPDIF_UPSAMPLE_4X_HYBRID_NS2);
                    appPanel.invalidateSettings();
                } else if (pressX >= 408 && pressX <= 470 &&
                           x >= 408 && x <= 470) {
                    USB_Audio_RequestSpdifMode(
                        USB_AUDIO_SPDIF_UPSAMPLE_4X_BUTTERWORTH_NS2);
                    appPanel.invalidateSettings();
                } else if (pressX >= 471 && pressX <= 533 &&
                           x >= 471 && x <= 533) {
                    USB_Audio_RequestSpdifMode(
                        USB_AUDIO_SPDIF_UPSAMPLE_4X_BUTTERWORTH_MINPHASE_NS2);
                    appPanel.invalidateSettings();
                } else if (pressX >= 534 && pressX <= 596 &&
                           x >= 534 && x <= 596) {
                    USB_Audio_RequestSpdifMode(
                        USB_AUDIO_SPDIF_UPSAMPLE_4X_BESSEL_MINPHASE_NS2);
                    appPanel.invalidateSettings();
                } else if (pressX >= 597 && pressX <= 659 &&
                           x >= 597 && x <= 659) {
                    USB_Audio_RequestSpdifMode(
                        USB_AUDIO_SPDIF_UPSAMPLE_4X_BESSEL_OPEN_NS2);
                    appPanel.invalidateSettings();
                } else if (pressX >= 660 && pressX <= 720 &&
                           x >= 660 && x <= 720) {
                    USB_Audio_RequestSpdifMode(
                        USB_AUDIO_SPDIF_UPSAMPLE_4X_BESSEL_MINPHASE_NS5);
                    appPanel.invalidateSettings();
                }
            } else if (contentPressY >= 432 && contentPressY <= 468 &&
                       contentReleaseY >= 432 &&
                       contentReleaseY <= 468 &&
                       pressX >= 220 && pressX <= 340 &&
                       x >= 220 && x <= 340) {
                USB_Audio_RequestSpdifMode(
                    USB_AUDIO_SPDIF_UPSAMPLE_4X_FIR_MINPHASE_NS5);
                appPanel.invalidateSettings();
            }
        } else if (selectedApp == 13U && x >= 235 && x <= 565 &&
                   y >= 335 && y <= 417 && pressX >= 235 && pressX <= 565 &&
                   pressY >= 335 && pressY <= 417) {
            AudioRecorderSnapshot snapshot;
            AudioRecorder_GetSnapshot(&snapshot);
            if (snapshot.status == AUDIO_RECORDER_RECORDING ||
                snapshot.status == AUDIO_RECORDER_STARTING) {
                AudioRecorder_RequestStop();
            } else if (snapshot.status != AUDIO_RECORDER_STOPPING) {
                AudioRecorder_RequestStart();
            }
            appPanel.invalidateRecorder();
        }
        return;
    }

    const int16_t dx = static_cast<int16_t>(x - pressX);
    const int16_t dy = static_cast<int16_t>(y - pressY);
    if (!dragging && dx > -12 && dx < 12 && dy > -12 && dy < 12) {
        /* Use the stable press coordinate for hit testing. The release point
           can drift just outside an icon even though this is still a tap. */
        const int16_t app = hitTestIcon(pressX, pressY);
        if (app >= 0) openApp(static_cast<uint8_t>(app));
        else settleToPage(currentPage);
        return;
    }

    uint8_t nextPage = currentPage;
    if (dx < -60) nextPage = 1;
    else if (dx > 60) nextPage = 0;
    else nextPage = stripOffset < -400 ? 1 : 0;
    if (nextPage != currentPage) ++desktop_swipe_count;
    settleToPage(nextPage);
}

void MainView::handleTrackingDrag(const DragEvent& event)
{
    if (appVisible) {
        if (selectedApp == 1U) {
            const int16_t deltaX = static_cast<int16_t>(event.getNewX() - pressX);
            const int16_t deltaY = static_cast<int16_t>(event.getNewY() - pressY);
            if (musicDetail) {
                if ((pressY >= 235 && pressY <= 282) ||
                    (pressY >= 385 && pressY <= 432)) {
                    if (deltaX <= -5 || deltaX >= 5 || deltaY <= -8 || deltaY >= 8) {
                        dragging = true;
                    }
                }
            } else if (pressY >= 175) {
                if (deltaY <= -8 || deltaY >= 8) dragging = true;
                const int16_t maxScroll = musicWavCount > 6U ?
                    static_cast<int16_t>(musicWavCount - 6U) : 0;
                int16_t next = static_cast<int16_t>(musicScrollStart - deltaY / 41);
                if (next < 0) next = 0;
                if (next > maxScroll) next = maxScroll;
                if (musicScroll != static_cast<uint8_t>(next)) {
                    musicScroll = static_cast<uint8_t>(next);
                    appPanel.setMusicScroll(musicScroll);
                }
            }
            return;
        } else if (selectedApp == 6U) {
            appPanel.setCalculatorPressedKey(
                calculatorKeyAt(event.getNewX(), event.getNewY()));
            return;
        } else if (selectedApp == 11U) {
            const int16_t deltaX = static_cast<int16_t>(event.getNewX() - pressX);
            const int16_t deltaY = static_cast<int16_t>(event.getNewY() - pressY);
            const int16_t contentPressY =
                static_cast<int16_t>(pressY + settingsScrollStart);
            if (pressY >= SETTINGS_VIEW_TOP &&
                pressY <= SETTINGS_VIEW_BOTTOM &&
                contentPressY >= 360 && contentPressY <= 402) {
                USB_AudioSnapshot audio;
                USB_Audio_GetSnapshot(&audio);
                if (audio.output == USB_AUDIO_OUTPUT_WM8994) {
                    if (deltaX <= -5 || deltaX >= 5 ||
                        deltaY <= -8 || deltaY >= 8) {
                        dragging = true;
                    }
                    USB_Audio_RequestVolume(
                        settingsGainFromX(event.getNewX()));
                    appPanel.invalidateSettingsGain();
                }
                return;
            }
            if (pressY >= SETTINGS_VIEW_TOP &&
                pressY <= SETTINGS_VIEW_BOTTOM &&
                contentPressY >= 636 && contentPressY <= 684) {
                if (deltaX <= -5 || deltaX >= 5 ||
                    deltaY <= -8 || deltaY >= 8) {
                    dragging = true;
                }
                USB_Audio_RequestStartFrames(
                    settingsStartFramesFromX(event.getNewX()));
                appPanel.invalidateSettingsStartBuffer();
                return;
            }
            if (pressY >= SETTINGS_VIEW_TOP &&
                pressY <= SETTINGS_VIEW_BOTTOM) {
                if (deltaY <= -8 || deltaY >= 8) dragging = true;
                if (dragging) {
                    int16_t next =
                        static_cast<int16_t>(settingsScrollStart - deltaY);
                    if (next < 0) next = 0;
                    if (next > SETTINGS_SCROLL_MAX) next = SETTINGS_SCROLL_MAX;
                    if (settingsScroll != next) {
                        settingsScroll = next;
                        appPanel.setSettingsScroll(settingsScroll);
                    }
                }
            }
            return;
        } else if (selectedApp == 2U && pressY >= 205) {
            const int16_t delta = static_cast<int16_t>(event.getNewY() - pressY);
            if (delta <= -8 || delta >= 8) dragging = true;
            const uint8_t entryCount = SD_Storage_GetCachedEntryCount();
            const int16_t maxScroll = entryCount > 6U ?
                static_cast<int16_t>(entryCount - 6U) : 0;
            int16_t next = static_cast<int16_t>(fileScrollStart - delta / 34);
            if (next < 0) next = 0;
            if (next > maxScroll) next = maxScroll;
            if (fileScroll != static_cast<uint8_t>(next)) {
                fileScroll = static_cast<uint8_t>(next);
                appPanel.setFileScroll(fileScroll);
            }
        } else if (selectedApp == 4U && pressY >= 282) {
            const int16_t delta = static_cast<int16_t>(event.getNewY() - pressY);
            if (delta <= -8 || delta >= 8) dragging = true;
            const uint8_t lapCount = Stopwatch_GetLapCount();
            const int16_t maxScroll = lapCount > 4U ? static_cast<int16_t>(lapCount - 4U) : 0;
            int16_t next = static_cast<int16_t>(stopwatchLapScrollStart - delta / 29);
            if (next < 0) next = 0;
            if (next > maxScroll) next = maxScroll;
            if (stopwatchLapScroll != static_cast<uint8_t>(next)) {
                stopwatchLapScroll = static_cast<uint8_t>(next);
                appPanel.setStopwatchLapScroll(stopwatchLapScroll);
            }
        } else if (selectedApp == 10U) {
            const int16_t deltaX = static_cast<int16_t>(event.getNewX() - pressX);
            const int16_t deltaY = static_cast<int16_t>(event.getNewY() - pressY);
            if (deltaX <= -8 || deltaX >= 8 || deltaY <= -8 || deltaY >= 8) {
                dragging = true;
            }
        }
        return;
    }
    const int16_t deltaX = static_cast<int16_t>(event.getNewX() - pressX);
    const int16_t deltaY = static_cast<int16_t>(event.getNewY() - pressY);
    if (!dragging) {
        /* FT6206 coordinate jitter must not turn a tap into a page swipe. */
        if (deltaX > -12 && deltaX < 12 && deltaY > -12 && deltaY < 12) return;
        dragging = true;
    }
    setStripOffset(static_cast<int16_t>(dragStartOffset + deltaX));
}

int16_t MainView::hitTestIcon(int16_t x, int16_t y) const
{
    for (uint8_t local = 0; local < 8; ++local) {
        const int16_t ix = ICON_X[local % 4];
        const int16_t iy = ICON_Y[local / 4];
        if (x >= ix && x < ix + 104 && y >= iy && y < iy + 112) {
            return static_cast<int16_t>(currentPage * 8 + local);
        }
    }
    return -1;
}

void MainView::openMusicFileAtRow(uint8_t row)
{
    SD_StorageSnapshot storage;
    SD_Storage_GetSnapshot(&storage);
    const SD_StorageEntry* entry = wavEntryAt(
        storage, static_cast<uint8_t>(musicScroll + row));
    if (entry == 0) return;
    AudioPlayer_RequestPlay(entry->name);
    musicDetail = true;
    appPanel.setMusicDetail(1U);
    appPanel.invalidateMusic();
}

void MainView::openApp(uint8_t app)
{
    if (app >= 16) return;
    selectedApp = app;
    appVisible = true;
    desktopBackground.setVisible(false);
    pageStrip.setVisible(false);
    appPanel.configure(app, APP_NAMES[app], APP_COLORS[app][0], APP_COLORS[app][1], APP_COLORS[app][2]);
    appPanel.setVisible(true);
    appPanel.invalidate();
    if (app == 1U) {
        musicScroll = 0U;
        musicScrollStart = 0U;
        musicWavCount = 0U;
        musicDetail = false;
        appPanel.setMusicScroll(0U);
        appPanel.setMusicDetail(0U);
        lastMusicGeneration = 0xFFFFFFFFUL;
        lastMusicStorageGeneration = 0xFFFFFFFFUL;
        lastMusicRenderSlot = 0xFFFFFFFFUL;
    } else if (app == 2U) {
        fileScroll = 0U;
        fileScrollStart = 0U;
        appPanel.setFileScroll(0U);
        lastStorageGeneration = 0xFFFFFFFFUL;
    } else if (app == 4U) {
        stopwatchLapScroll = 0U;
        stopwatchLapScrollStart = 0U;
        appPanel.setStopwatchLapScroll(0U);
        lastStopwatchRenderSlot = 0xFFFFFFFFUL;
        lastStopwatchControlGeneration = stopwatch_control_generation;
    } else if (app == 10U) {
        lastSpectrumGeneration = 0xFFFFFFFFUL;
        lastSpectrumRenderSlot = 0xFFFFFFFFUL;
        AudioSpectrum_RequestStart();
    } else if (app == 11U) {
        settingsScroll = 0;
        settingsScrollStart = 0;
        appPanel.setSettingsScroll(0);
        lastUsbMscGeneration = 0xFFFFFFFFUL;
        lastUsbAudioGeneration = 0xFFFFFFFFUL;
    } else if (app == 13U) {
        lastRecorderGeneration = 0xFFFFFFFFUL;
    } else if (app == 15U) {
        lastSystemRenderSecond = 0xFFFFFFFFUL;
    }
    desktop_current_app = app;
    ++desktop_app_open_count;
}

void MainView::closeApp()
{
    if (selectedApp == 1U && AudioPlayer_IsBusy() != 0U) {
        AudioPlayer_RequestStop();
    }
    if (selectedApp == 13U && AudioRecorder_IsBusy() != 0U) {
        AudioRecorder_RequestStop();
    }
    if (selectedApp == 10U) AudioSpectrum_RequestStop();
    if (selectedApp == 6U) appPanel.setCalculatorPressedKey(0);
    appVisible = false;
    selectedApp = 0xFF;
    appPanel.setVisible(false);
    desktopBackground.setVisible(true);
    pageStrip.setVisible(true);
    desktopBackground.invalidate();
    pageStrip.invalidate();
    desktop_current_app = 0xFF;
    ++desktop_app_close_count;
}
