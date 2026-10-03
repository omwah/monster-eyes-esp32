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
    // The caller composites transient UI onto the same framebuffer before
    // flushing. Flushing in frameEnd() would show an eye-only frame briefly
    // before the name overlay on every tick, making the name flicker.
    void flushFrame() {
        const uint32_t start = micros();
        _panel.flush();
        _flushUs += micros() - start;
    }
    uint32_t takeFlushUs() {
        const uint32_t elapsed = _flushUs;
        _flushUs = 0;
        return elapsed;
    }
    uint32_t takeScaleUs() {
        const uint32_t elapsed = _scaleUs;
        _scaleUs = 0;
        return elapsed;
    }
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
    // Hold the name's narrow horizontal band unchanged while the eye animates.
    // It is drawn once rather than erased and repainted every frame.
    void setNameBand(bool visible) { _nameBand = visible; }
protected:
    void panelSize(int *w, int *h) override { *w = PANEL_W; *h = PANEL_H; }
    void flushStripe(int, int x0, int width, uint16_t *pixels) override {
        const uint32_t start = micros();
        const int size = eyeSize();
        const int left = x0 * PANEL_W / size;
        const int right = (x0 + width) * PANEL_W / size;
        const int scaledWidth = right - left;
        uint16_t *frame = _panel.getFramebuffer();
        if (size == 240 && _top == 0 && _bottom == 0) {
            // Exact 2x: write two adjacent pixels in one 32-bit store, then
            // copy the completed row to its neighbour. No divisions per pixel.
            for (int row = 0; row < size; ++row) {
                if (_nameBand && row >= 190 && row < 209) continue;
                const uint16_t *src = pixels + size_t(row) * width;
                uint16_t *dst = frame + size_t(row * 2) * PANEL_W + left;
                uint32_t *pair = reinterpret_cast<uint32_t *>(dst);
                uint32_t *next = reinterpret_cast<uint32_t *>(dst + PANEL_W);
                for (int col = 0; col < width; ++col) {
                    const uint32_t doubled = uint32_t(src[col]) * 0x10001U;
                    pair[col] = doubled;
                    next[col] = doubled;
                }
            }
        } else {
            for (int y = _top; y < PANEL_H - _bottom; ++y) {
                if (_nameBand && y >= 380 && y < 418) continue;
                const uint16_t *src = pixels + size_t(y * size / PANEL_H) * width;
                uint16_t *dst = frame + size_t(y) * PANEL_W + left;
                for (int x = left; x < right; ++x)
                    *dst++ = src[x * size / PANEL_W - x0];
            }
        }
        _scaleUs += micros() - start;
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
    bool _nameBand = false;
    uint32_t _flushUs = 0;
    uint32_t _scaleUs = 0;
};
