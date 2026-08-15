#ifndef MAINVIEW_HPP
#define MAINVIEW_HPP

#include <gui/main_screen/MainPresenter.hpp>
#include <touchgfx/Color.hpp>
#include <touchgfx/containers/Container.hpp>
#include <touchgfx/events/ClickEvent.hpp>
#include <touchgfx/events/DragEvent.hpp>
#include <mvp/View.hpp>
#include <touchgfx/widgets/TouchArea.hpp>
#include <touchgfx/widgets/Widget.hpp>

class ProceduralWidget : public touchgfx::Widget
{
public:
    virtual touchgfx::Rect getSolidRect() const { return touchgfx::Rect(); }

protected:
    void fillLocal(const touchgfx::Rect& bounds,
                   const touchgfx::Rect& dirty,
                   touchgfx::colortype color) const;
    void fillRounded(int16_t x, int16_t y, int16_t w, int16_t h, int16_t radius,
                     touchgfx::colortype color, const touchgfx::Rect& dirty) const;
    void drawText(const char* text, int16_t x, int16_t y, uint8_t scale,
                  touchgfx::colortype color, const touchgfx::Rect& dirty) const;
    void drawCloseX(int16_t cx, int16_t cy, touchgfx::colortype color,
                    const touchgfx::Rect& dirty) const;
};

class DesktopBackgroundWidget : public ProceduralWidget
{
public:
    DesktopBackgroundWidget();
    void setPage(uint8_t page);
    virtual void draw(const touchgfx::Rect& invalidatedArea) const;

private:
    uint8_t currentPage;
};

class DesktopIconWidget : public ProceduralWidget
{
public:
    DesktopIconWidget();
    void configure(uint8_t index, const char* name, uint8_t r, uint8_t g, uint8_t b);
    virtual void draw(const touchgfx::Rect& invalidatedArea) const;

private:
    void drawSymbol(const touchgfx::Rect& dirty) const;
    uint8_t appIndex;
    const char* label;
    touchgfx::colortype accent;
};

class AppPanelWidget : public ProceduralWidget
{
public:
    AppPanelWidget();
    void configure(uint8_t index, const char* name, uint8_t r, uint8_t g, uint8_t b);
    void invalidateStopwatchTime();
    void invalidateStopwatchStartButton();
    void invalidateStopwatchLaps();
    void invalidateStopwatchBody();
    void setStopwatchLapScroll(uint8_t offset);
    void setFileScroll(uint8_t offset);
    void setMusicScroll(uint8_t offset);
    void setMusicDetail(uint8_t detail);
    void invalidateFiles();
    void invalidateMusic();
    void invalidateSystemUsage();
    void invalidateRecorder();
    void invalidateCalculator();
    void invalidateSpectrum();
    void invalidateSettings();
    void invalidateSettingsGain();
    void calculatorPress(char key);
    void setCalculatorPressedKey(char key);
    virtual void draw(const touchgfx::Rect& invalidatedArea) const;

private:
    void drawCards(const touchgfx::Rect& dirty) const;
    void drawFiles(const touchgfx::Rect& dirty) const;
    void drawMusic(const touchgfx::Rect& dirty) const;
    void drawMusicList(const touchgfx::Rect& dirty) const;
    void drawMusicPlayer(const touchgfx::Rect& dirty) const;
    void drawStopwatch(const touchgfx::Rect& dirty) const;
    void drawSystemUsage(const touchgfx::Rect& dirty) const;
    void drawRecorder(const touchgfx::Rect& dirty) const;
    void drawCalculator(const touchgfx::Rect& dirty) const;
    void drawSpectrum(const touchgfx::Rect& dirty) const;
    void drawSettings(const touchgfx::Rect& dirty) const;
    void resetCalculator();
    double calculatorValue() const;
    bool applyCalculatorOperation(double right);
    uint8_t appIndex;
    const char* label;
    uint8_t accentR;
    uint8_t accentG;
    uint8_t accentB;
    uint8_t stopwatchLapScroll;
    uint8_t fileScroll;
    uint8_t musicScroll;
    uint8_t musicDetail;
    char calculatorInput[24];
    char calculatorHistory[56];
    uint8_t calculatorLength;
    double calculatorAccumulator;
    char calculatorOperation;
    char calculatorPressedKey;
    bool calculatorReplaceInput;
    bool calculatorError;
};

class MainView;

class TrackingTouchArea : public touchgfx::TouchArea
{
public:
    TrackingTouchArea();
    void setOwner(MainView* view);
    virtual void handleClickEvent(const touchgfx::ClickEvent& event);
    virtual void handleDragEvent(const touchgfx::DragEvent& event);

private:
    MainView* owner;
};

class MainView : public touchgfx::View<MainPresenter>
{
public:
    MainView();
    virtual ~MainView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleTickEvent();

    void handleTrackingClick(const touchgfx::ClickEvent& event);
    void handleTrackingDrag(const touchgfx::DragEvent& event);

private:
    void setStripOffset(int16_t offset);
    void settleToPage(uint8_t page);
    void openApp(uint8_t app);
    void closeApp();
    void openMusicFileAtRow(uint8_t row);
    int16_t hitTestIcon(int16_t x, int16_t y) const;

    DesktopBackgroundWidget desktopBackground;
    touchgfx::Container pageStrip;
    touchgfx::Container page0;
    touchgfx::Container page1;
    DesktopIconWidget icons[16];
    AppPanelWidget appPanel;
    TrackingTouchArea touchArea;

    bool appVisible;
    bool dragging;
    bool animating;
    uint8_t currentPage;
    uint8_t selectedApp;
    int16_t pressX;
    int16_t pressY;
    int16_t dragStartOffset;
    int16_t stripOffset;
    int16_t targetOffset;
    uint8_t stopwatchLapScroll;
    uint8_t stopwatchLapScrollStart;
    uint8_t fileScroll;
    uint8_t fileScrollStart;
    uint8_t musicScroll;
    uint8_t musicScrollStart;
    uint8_t musicWavCount;
    bool musicDetail;
    uint32_t lastStopwatchRenderSlot;
    uint32_t lastStopwatchControlGeneration;
    uint32_t lastStorageGeneration;
    uint32_t lastMusicGeneration;
    uint32_t lastMusicStorageGeneration;
    uint32_t lastMusicRenderSlot;
    uint32_t lastSystemRenderSecond;
    uint32_t lastRecorderGeneration;
    uint32_t lastSpectrumGeneration;
    uint32_t lastSpectrumRenderSlot;
    uint32_t lastUsbMscGeneration;
    uint32_t lastUsbAudioGeneration;
};

#endif
