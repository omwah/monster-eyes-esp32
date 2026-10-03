// LilyGo T-RGB 2.1-inch (H583/H597): one eye on the 480x480 ST7701 RGB panel.
// Pin map and timings from LilyGo's Arduino_GFX_HelloWorld example.
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include "monster_controller.h"
#include "trgb_touch.h"

namespace {
Arduino_DataBus *bus = new Arduino_XL9535SWSPI(8, 48, 2, 3, 5, 4);
Arduino_ESP32RGBPanel *rgb = new Arduino_ESP32RGBPanel(
    45, 41, 47, 42,
    21, 18, 17, 16, 15,
    14, 13, 12, 11, 10, 9,
    7, 6, 5, 3, 2,
    1, 50, 1, 30, 1, 20, 1, 30, 1);
Arduino_RGB_Display panel(480, 480, rgb, 0, false, bus, GFX_NOT_DEFINED,
                          st7701_type4_init_operations,
                          sizeof(st7701_type4_init_operations));
CompositeTftDisplay backend(panel);
MonsterController monster(backend);
TrgbTouch touch;
constexpr int BACKLIGHT_PIN = 46;
constexpr int BUTTON_PIN = 0;
bool buttonWasDown = false;
uint32_t buttonDownAt = 0;
uint32_t nextFrame = 0;
uint32_t fpsWindowStart = 0;
uint32_t framesInWindow = 0;
uint64_t renderUsInWindow = 0;
uint64_t flushUsInWindow = 0;
uint64_t scaleUsInWindow = 0;
uint32_t nameUntil = 0;
bool nameDrawn = false;
bool touching = false;
int touchStartX = 0, touchStartY = 0;
int touchLastX = 0, touchLastY = 0;
uint32_t touchStartAt = 0;
uint8_t missedTouchReads = 0;

void hideName() {
    nameUntil = 0;
    nameDrawn = false;
    backend.setNameBand(false);
}

void showName(uint32_t now) {
    nameUntil = now + 4000;
    nameDrawn = false;
    backend.setNameBand(true);
}

void switchStyle(bool next, uint32_t now) {
    if (next ? monster.nextStyle() : monster.previousStyle()) {
        panel.fillScreen(monster.screenBackground());
        showName(now);
        Serial.printf("style: %s\n", monster.styleName(monster.style()));
    }
}

void drawName() {
    // The round panel is only ~320px wide at y=398. Stay well within it.
    String name = monster.styleName(monster.style());
    if (name.length() > 24) name = name.substring(0, 24);
    panel.fillRoundRect(80, 380, 320, 38, 8, 0);
    panel.setTextSize(2);
    panel.setTextColor(0xffff, 0);
    panel.setCursor(240 - name.length() * 6, 391);
    panel.print(name);
}
} // namespace

void setup() {
    Serial.begin(115200);
    pinMode(BACKLIGHT_PIN, OUTPUT);
    digitalWrite(BACKLIGHT_PIN, LOW);
    Wire.begin(8, 48, 400000);
    if (!panel.begin()) {
        Serial.println("T-RGB panel init failed");
        while (true) delay(1000);
    }
    panel.fillScreen(0);
    digitalWrite(BACKLIGHT_PIN, HIGH);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    touch.begin();
    if (!monster.begin()) {
        Serial.println("No eye packages; upload the FFat filesystem (uploadfs)");
        panel.setTextColor(0xffff);
        panel.setCursor(60, 230);
        panel.print("Upload eye packages (FFat)");
        while (true) delay(1000);
    }
    panel.fillScreen(monster.screenBackground());
    showName(millis());
    Serial.println("T-RGB single eye ready");
}

void loop() {
    const uint32_t now = millis();
    const bool down = digitalRead(BUTTON_PIN) == LOW;
    if (down && !buttonWasDown) buttonDownAt = now;
    if (!down && buttonWasDown && now - buttonDownAt >= 30) {
        switchStyle(true, now);
    }
    buttonWasDown = down;

    int x = 0, y = 0;
    bool pressed = touch.read(x, y);
    const int rx = x - 240, ry = y - 240;
    if (pressed && rx * rx + ry * ry > 235 * 235) pressed = false;
    if (pressed) {
        missedTouchReads = 0;
        if (!touching) {
            touching = true;
            touchStartX = x;
            touchStartY = y;
            touchStartAt = now;
            Serial.printf("touch start: %d,%d\n", x, y);
        }
        touchLastX = x;
        touchLastY = y;
        const int dx = x - touchStartX, dy = y - touchStartY;
        if (dx * dx + dy * dy > 25 * 25) hideName();
        monster.setGaze(constrain((240.0f - x) / 240.0f, -1.0f, 1.0f),
                        constrain((y - 240.0f) / 240.0f, -1.0f, 1.0f));
    } else if (touching && ++missedTouchReads >= 2) {
        touching = false;
        monster.releaseGaze();
        const int dx = touchLastX - touchStartX;
        const int dy = touchLastY - touchStartY;
        const uint32_t duration = now - touchStartAt;
        Serial.printf("touch end: %d,%d delta %d,%d (%lu ms)\n",
                      touchLastX, touchLastY, dx, dy, (unsigned long)duration);
        if (abs(dx) >= 70 && abs(dx) > abs(dy) * 3 / 2 && duration < 1500)
            switchStyle(dx < 0, now);
        else if (dx * dx + dy * dy < 18 * 18 && duration < 500)
            monster.blink();
    }

    // Debug capture: request 'S' on USB serial for a raw 480x480 RGB565
    // framebuffer snapshot. Pause rendering while the bytes are sent.
    if (Serial.available() && Serial.read() == 'S') {
        monster.animate();
        backend.flushFrame();
        Serial.write((const uint8_t *)"FRAME565\n", 9);
        const uint8_t *frame = (const uint8_t *)panel.getFramebuffer();
        for (size_t offset = 0; offset < 480UL * 480 * 2; offset += 960)
            Serial.write(frame + offset, 960);
        Serial.flush();
    }
    if (int32_t(now - nextFrame) >= 0) {
        if (nameUntil && int32_t(now - nameUntil) >= 0) hideName();
        const uint32_t renderStart = micros();
        monster.animate();
        if (nameUntil && !nameDrawn && !touching) {
            drawName();
            nameDrawn = true;
        }
        backend.flushFrame(); // present eye and name atomically
        renderUsInWindow += micros() - renderStart;
        flushUsInWindow += backend.takeFlushUs();
        scaleUsInWindow += backend.takeScaleUs();
        ++framesInWindow;
        nextFrame = now + 33;
    }
    if (!fpsWindowStart) fpsWindowStart = now;
    const uint32_t elapsed = now - fpsWindowStart;
    if (elapsed >= 5000) {
        Serial.printf("animation FPS: %.1f (%lu frames / %lu ms), "
                      "frame %.1f ms, scale %.1f ms, flush %.1f ms\n",
                      framesInWindow * 1000.0f / elapsed,
                      (unsigned long)framesInWindow, (unsigned long)elapsed,
                      framesInWindow ? renderUsInWindow / (framesInWindow * 1000.0f) : 0.0f,
                      framesInWindow ? scaleUsInWindow / (framesInWindow * 1000.0f) : 0.0f,
                      framesInWindow ? flushUsInWindow / (framesInWindow * 1000.0f) : 0.0f);
        framesInWindow = 0;
        renderUsInWindow = flushUsInWindow = scaleUsInWindow = 0;
        fpsWindowStart = now;
    }
    delay(1);
}
