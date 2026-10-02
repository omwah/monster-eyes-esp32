#pragma once

#include <Adafruit_Monster_Eyes.h>
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>
#include <cstring>

// ST7701 RGB panel adapter. Render at the engine's supported 240px size
// (its eyelid coordinates are uint8_t), then scale to fill the 480px panel.
// RGB pixels live in the driver's PSRAM framebuffer; no SPI transaction or
// endian swap is involved.
class CompositeTftDisplay : public Eyes_StripeDisplay {
public:
    static constexpr int PANEL_W = 480, PANEL_H = 480;
    explicit CompositeTftDisplay(Arduino_RGB_Display &panel)
        : Eyes_StripeDisplay(1, 16), _panel(panel) {}
    ~CompositeTftDisplay() { free(_mirror); }
    bool begin() override { return true; } // panel initialized in setup()
    void frameEnd() override { _panel.flush(); } // one cache flush per frame
    bool setEyeCount(uint8_t eyes) override { return eyes == 1; }
    void clear(uint16_t color) override {
        _panel.fillScreen(color);
        _panel.flush();
        _background = color;
        if (_mirror)
            for (size_t i = 0; i < size_t(PANEL_W) * PANEL_H; ++i) _mirror[i] = color;
    }
    bool armCapture() {
        if (!_mirror) {
            _mirror = (uint16_t *)heap_caps_malloc(size_t(PANEL_W) * PANEL_H * 2, MALLOC_CAP_SPIRAM);
            if (!_mirror) return false;
            for (size_t i = 0; i < size_t(PANEL_W) * PANEL_H; ++i) _mirror[i] = _background;
        }
        _capturing = true;
        return true;
    }
    void disarmCapture() { _capturing = false; }
    const uint16_t *mirror() const { return _mirror; }
    int eyeOriginX() const { return originX(); }
    int eyeOriginY() const { return originY(); }
    void setReservedRows(int top, int bottom) { _top = top; _bottom = bottom; }
protected:
    void panelSize(int *w, int *h) override { *w = PANEL_W; *h = PANEL_H; }
    void flushStripe(int, int x0, int width, uint16_t *pixels) override {
        const int size = eyeSize();
        const int left = x0 * PANEL_W / size;
        const int right = (x0 + width) * PANEL_W / size;
        const int scaledWidth = right - left;
        uint16_t *frame = _panel.getFramebuffer();
        for (int y = _top; y < PANEL_H - _bottom; ++y) {
            const uint16_t *src = pixels + size_t(y * size / PANEL_H) * width;
            uint16_t *dst = frame + size_t(y) * PANEL_W + left;
            for (int x = left; x < right; ++x)
                *dst++ = src[x * size / PANEL_W - x0];
        }
        if (!_capturing || !_mirror) return;
        for (int row = 0; row < PANEL_H; ++row)
            memcpy(_mirror + size_t(row) * PANEL_W + left,
                   frame + size_t(row) * PANEL_W + left,
                   size_t(scaledWidth) * 2);
    }
private:
    Arduino_RGB_Display &_panel;
    uint16_t *_mirror = nullptr;
    uint16_t _background = 0;
    int _top = 0, _bottom = 0;
    bool _capturing = false;
};
