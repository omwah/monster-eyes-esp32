// LilyGo T-RGB 2.1-inch (H583/H597): one eye on the 480x480 ST7701 RGB panel.
// Pin map and timings from LilyGo's Arduino_GFX_HelloWorld example.
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include "monster_controller.h"

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
constexpr int BACKLIGHT_PIN = 46;
constexpr int BUTTON_PIN = 0;
bool buttonWasDown = false;
uint32_t buttonDownAt = 0;
uint32_t nextFrame = 0;
uint32_t fpsWindowStart = 0;
uint32_t framesInWindow = 0;
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
    if (!monster.begin()) {
        Serial.println("No eye packages; upload the FFat filesystem (uploadfs)");
        panel.setTextColor(0xffff);
        panel.setCursor(60, 230);
        panel.print("Upload eye packages (FFat)");
        while (true) delay(1000);
    }
    panel.fillScreen(monster.screenBackground());
    Serial.println("T-RGB single eye ready");
}

void loop() {
    const uint32_t now = millis();
    const bool down = digitalRead(BUTTON_PIN) == LOW;
    if (down && !buttonWasDown) buttonDownAt = now;
    if (!down && buttonWasDown) {
        if (now - buttonDownAt >= 700) {
            monster.nextStyle();
            panel.fillScreen(monster.screenBackground());
        } else {
            monster.blink();
        }
    }
    buttonWasDown = down;
    // Debug capture: request 'S' on USB serial for a raw 480x480 RGB565
    // framebuffer snapshot. Pause rendering while the bytes are sent.
    if (Serial.available() && Serial.read() == 'S') {
        monster.animate();
        Serial.write((const uint8_t *)"FRAME565\n", 9);
        const uint8_t *frame = (const uint8_t *)panel.getFramebuffer();
        for (size_t offset = 0; offset < 480UL * 480 * 2; offset += 960)
            Serial.write(frame + offset, 960);
        Serial.flush();
    }
    if (int32_t(now - nextFrame) >= 0) {
        monster.animate();
        ++framesInWindow;
        nextFrame = now + 33;
    }
    if (!fpsWindowStart) fpsWindowStart = now;
    const uint32_t elapsed = now - fpsWindowStart;
    if (elapsed >= 5000) {
        Serial.printf("animation FPS: %.1f (%lu frames / %lu ms)\n",
                      framesInWindow * 1000.0f / elapsed,
                      (unsigned long)framesInWindow, (unsigned long)elapsed);
        framesInWindow = 0;
        fpsWindowStart = now;
    }
    delay(1);
}
