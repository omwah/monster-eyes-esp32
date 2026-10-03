#pragma once

#include <Arduino.h>
#include <Wire.h>

// Both 2.1-inch T-RGB touch variants use the same first-point registers:
// CST820 at 0x15, FT3267 at 0x38. The display's XL9535 SPI bus initializes
// the expander before this is polled, so the touch reset line is released.
class TrgbTouch {
public:
    bool begin() {
        for (uint8_t address : {uint8_t(0x15), uint8_t(0x38)}) {
            Wire.beginTransmission(address);
            if (Wire.endTransmission() == 0) {
                _address = address;
                Serial.printf("T-RGB touch controller at 0x%02X\n", _address);
                return true;
            }
        }
        Serial.println("T-RGB touch controller not found");
        return false;
    }

    bool read(int &x, int &y) {
        if (!_address) return false;
        Wire.beginTransmission(_address);
        Wire.write(0x02); // number of points followed by first X/Y pair
        if (Wire.endTransmission(false) != 0 ||
            Wire.requestFrom(int(_address), 5) != 5)
            return false;
        const uint8_t count = Wire.read() & 0x0f;
        const uint8_t xh = Wire.read(), xl = Wire.read();
        const uint8_t yh = Wire.read(), yl = Wire.read();
        if (count != 1 && count != 2) return false;
        x = ((xh & 0x0f) << 8) | xl;
        y = ((yh & 0x0f) << 8) | yl;
        return x >= 0 && x < 480 && y >= 0 && y < 480;
    }

private:
    uint8_t _address = 0;
};
