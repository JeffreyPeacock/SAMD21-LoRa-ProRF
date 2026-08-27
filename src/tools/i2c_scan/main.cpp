// i2c_scan -- what is actually on the Qwiic chain?
//
// No radio, no external libraries, nothing to misconfigure. When a sensor
// "does not work", this separates "the device is not on the bus" from "the
// device is on the bus and the driver is unhappy", and those have completely
// different fixes.

#include <Arduino.h>
#include <Wire.h>

#include <board.h>

namespace {

const char* nameForAddress(uint8_t addr) {
    switch (addr) {
        case board::kAddrOpenLog:   return "Qwiic OpenLog";
        case 0x5A:                  return "CCS811 air quality (alternate address)";
        case board::kAddrCcs811:    return "CCS811 air quality";
        case 0x76:                  return "BME280 environment (alternate address)";
        case board::kAddrBme280:    return "BME280 environment";
        default:                    return "unknown";
    }
}

void scan() {
    int found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() != 0) continue;
        found++;
        SerialUSB.print(F("  0x"));
        if (addr < 0x10) SerialUSB.print('0');
        SerialUSB.print(addr, HEX);
        SerialUSB.print(F("  "));
        SerialUSB.println(nameForAddress(addr));
    }

    if (found == 0) {
        SerialUSB.println(F("  nothing answered."));
        SerialUSB.println(F("  Qwiic is one shared bus, not a hub: a single"));
        SerialUSB.println(F("  unseated or reversed cable removes every device"));
        SerialUSB.println(F("  downstream of it. Suspect the cable before the"));
        SerialUSB.println(F("  three sensors that all went quiet at once."));
    } else {
        SerialUSB.print(F("  "));
        SerialUSB.print(found);
        SerialUSB.println(F(" device(s)."));
    }
}

}  // namespace

void setup() {
    pinMode(board::kPinStatusLed, OUTPUT);
    SerialUSB.begin(115200);
    const uint32_t deadline = millis() + 10000;
    while (!SerialUSB && millis() < deadline) delay(10);

    Wire.begin();
    Wire.setClock(board::kI2cClockHz);

    SerialUSB.println();
    SerialUSB.println(F("== Qwiic / I2C scan =============================="));
    scan();
    SerialUSB.println(F("  (press any key to rescan)"));
}

void loop() {
    if (SerialUSB.available()) {
        while (SerialUSB.available()) SerialUSB.read();
        digitalWrite(board::kPinStatusLed, HIGH);
        SerialUSB.println();
        scan();
        digitalWrite(board::kPinStatusLed, LOW);
    }
    delay(50);
}
