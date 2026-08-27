// qwiic_read -- STAGE 2: read all the sensors reliably, in a repeating cycle.
//
// No logging, no radio, deliberately. A sensor problem must not be diagnosed
// through two other subsystems; those come after this is stable. See
//
// One slot per device, N slots per cycle, repeat forever. Each slot:
//

#include <Arduino.h>
#include <Wire.h>

#include <board.h>
#include <nmea.h>
#include <qwiic.h>
#include <rtc.h>

namespace {

// A full cycle is kDeviceCount slots. 250 ms each gives a 1 s cycle, which
// matches the slowest useful device: the CCS811 produces one result per
// second in its default drive mode, so a faster cycle only wastes bus time.
// Sized from that constraint, then confirmed against the measured per-device
// read times the health report prints.
constexpr uint32_t kSlotMs        = 250;
constexpr uint32_t kHealthEveryMs = 15000;

uint32_t g_seq       = 0;
uint8_t  g_slot      = 0;
uint32_t g_slotStart = 0;
uint32_t g_lastHealth = 0;
uint32_t g_cycles    = 0;

char g_sentence[128];
char g_line[160];

void health() {
    SerialUSB.println();
    SerialUSB.print(F("-- health after "));
    SerialUSB.print(g_cycles);
    SerialUSB.print(F(" cycles, up "));
    SerialUSB.print(millis() / 1000);
    SerialUSB.println(F(" s ------------------"));
    for (uint8_t i = 0; i < qwiic::kDeviceCount; i++) {
        qwiic::formatStats(static_cast<qwiic::DeviceId>(i), g_line, sizeof(g_line));
        SerialUSB.println(g_line);
    }
    SerialUSB.print(F("  clock   "));
    if (rtc::describe(g_line, sizeof(g_line))) {
        SerialUSB.println(g_line);
    } else {
        SerialUSB.println(F("NOT SET -- run `make clock`; timestamps are uptime"));
    }
    SerialUSB.println(F("--------------------------------------------------"));
}

}  // namespace

void setup() {
    pinMode(board::kPinStatusLed, OUTPUT);
    SerialUSB.begin(115200);
    const uint32_t deadline = millis() + 10000;
    while (!SerialUSB && millis() < deadline) delay(10);

    SerialUSB.println();
    SerialUSB.println(F("== qwiic_read -- slot scheduler ==================="));
    qwiic::begin();
    rtc::begin();
    SerialUSB.print(F("  slot "));
    SerialUSB.print(kSlotMs);
    SerialUSB.print(F(" ms x "));
    SerialUSB.print(qwiic::kDeviceCount);
    SerialUSB.print(F(" devices = "));
    SerialUSB.print(kSlotMs * qwiic::kDeviceCount);
    SerialUSB.println(F(" ms cycle"));
    SerialUSB.println(F("  an empty field means no reading, not zero"));
    SerialUSB.println(F("  set the clock with:  make clock"));
    SerialUSB.println(F("--------------------------------------------------"));

    g_slotStart  = millis();
    g_lastHealth = millis();
}

void loop() {
    rtc::pollSerial();

    const uint32_t now = millis();
    if ((now - g_slotStart) < kSlotMs) {
        delay(2);
        return;
    }
    g_slotStart = now;

    const qwiic::DeviceId id = static_cast<qwiic::DeviceId>(g_slot);
    const uint8_t len = qwiic::readInto(id, g_sentence, sizeof(g_sentence));

    if (len > 0) {
        digitalWrite(board::kPinStatusLed, HIGH);
        char stamp[16];
        rtc::stamp(stamp, sizeof(stamp));
        snprintf(g_line, sizeof(g_line), "%s %04lu:%s",
                 stamp, static_cast<unsigned long>(g_seq % 10000), g_sentence);
        SerialUSB.println(g_line);
        g_seq++;
        digitalWrite(board::kPinStatusLed, LOW);
    }

    if (++g_slot >= qwiic::kDeviceCount) {
        g_slot = 0;
        g_cycles++;
    }

    if ((millis() - g_lastHealth) >= kHealthEveryMs) {
        g_lastHealth = millis();
        health();
    }
}
