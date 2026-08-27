#include "qwiic.h"

#include <Arduino.h>
#include <Wire.h>

#include <SparkFunBME280.h>
#include <SparkFunCCS811.h>

#include <board.h>
#include <nmea.h>

namespace qwiic {
namespace {

// Bus speed, overridable at build time:  make flash-qwiic_read I2CHZ=100000
//
// The OpenLog needs a far slower bus than the sensors do, and being able to
// sweep this from the command line is how that was established.
#ifndef QWIIC_I2C_HZ
#define QWIIC_I2C_HZ 400000UL
#endif
constexpr uint32_t kI2cClockHz = QWIIC_I2C_HZ;

BME280      g_bme;
CCS811      g_ccs(board::kAddrCcs811);

Device g_dev[kDeviceCount];


// Latest good values, held between slots. A device's slot emits what it has;
// if nothing new arrived, the slot reports a gap rather than repeating a stale
// reading as though it were fresh.
struct {
    float tempC = 0, humidity = 0, pressureMb = 0, altitudeM = 0;
    bool  fresh = false;
} g_env;

struct {
    uint16_t co2 = 0, tvoc = 0;
    bool     fresh = false;
} g_air;


// Retried, because a single probe is not a reliable presence test.
//
// Observed 2026-08-22: node reported all four sensors absent, and then
// datalog::begin() probed 0x2A successfully a few milliseconds later IN THE
// SAME RUN -- while an i2c_scan flashed seconds afterwards found every device.
// The bus was fine; the FIRST transactions after Wire.begin() were not.
//
// Three attempts with a short gap. The cost when a device really is absent is
// ~15 ms; the cost of getting it wrong is a sensor silently dropped for the
// entire session.
bool addressResponds(uint8_t addr) {
    for (uint8_t attempt = 0; attempt < 3; attempt++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) return true;
        delay(5);
    }
    return false;
}

void record(DeviceId id, bool ok, uint32_t micros_) {
    Stats& s = g_dev[id].stats;
    s.attempts++;
    if (ok) {
        s.ok++;
        s.consecutiveFailures = 0;
        if (micros_ < s.minMicros) s.minMicros = micros_;
        if (micros_ > s.maxMicros) s.maxMicros = micros_;
        s.totalMicros += micros_;
    } else {
        s.failed++;
        s.consecutiveFailures++;
        if (s.consecutiveFailures > s.worstStreak) s.worstStreak = s.consecutiveFailures;
    }
}

// Small append helpers shared by every formatter. Everything bails out on
// overflow rather than truncating: a short sentence carrying a valid checksum
// is worse than no sentence at all.
struct Appender {
    char*   buf;
    uint8_t cap;
    size_t  at;
    bool    overflowed;

    // Explicit constructor rather than a braced initialiser: the Arduino SAMD
    // core builds with -std=gnu++11, where a struct carrying default member
    // initialisers is not an aggregate and cannot be brace-initialised.
    Appender(char* b, uint8_t c) : buf(b), cap(c), at(0), overflowed(false) {}

    void put(const char* s) {
        const size_t l = strlen(s);
        if (at + l >= cap) { overflowed = true; return; }
        memcpy(buf + at, s, l);
        at += l;
    }
    void u(uint32_t v)  { char n[12]; ultoa(v, n, 10); put(n); }
    void f(float v, uint8_t d, bool present) {
        if (!present) return;
        char n[20]; nmea::f(n, v, d); put(n);
    }
    uint8_t finish() {
        if (overflowed || at >= cap) return 0;
        buf[at] = '\0';
        return nmea::appendChecksum(buf, cap);
    }
};

}  // namespace


namespace {

// Free a bus that a device is holding down.
//
// I2C has no reset. If a slave is interrupted mid-byte -- by the master
// resetting, which is exactly what an upload does -- it can be left driving
// SDA low waiting to finish a transfer that will never come. The bus is then
// dead for EVERY device, and the symptom is that nothing at all answers, which
// reads as "the whole chain is unplugged".
//
// Seen here 2026-08-22: the node reported all four sensors absent while the
// OpenLog answered, and an i2c_scan run seconds later found four devices with
// devices gone at once. A stuck slave explains that.
//
// The standard recovery: take SCL as a GPIO and clock it up to nine times with
// SDA released. Nine because that is a byte plus the ACK -- enough for any
// slave to finish whatever it was mid-way through and let go. Then issue a
// STOP so everyone is back in idle.
void recoverBus() {
    const uint8_t sda = PIN_WIRE_SDA;
    const uint8_t scl = PIN_WIRE_SCL;

    pinMode(sda, INPUT_PULLUP);
    pinMode(scl, INPUT_PULLUP);
    delayMicroseconds(10);

    if (digitalRead(sda) != LOW) return;   // nobody is holding it; nothing to do

    SerialUSB.println(F("  I2C bus  SDA held LOW -- clocking the bus free"));

    pinMode(scl, OUTPUT);
    for (uint8_t i = 0; i < 9 && digitalRead(sda) == LOW; i++) {
        digitalWrite(scl, LOW);
        delayMicroseconds(5);
        digitalWrite(scl, HIGH);
        delayMicroseconds(5);
    }

    // Manual STOP: SDA low->high while SCL is high.
    pinMode(sda, OUTPUT);
    digitalWrite(sda, LOW);
    delayMicroseconds(5);
    digitalWrite(scl, HIGH);
    delayMicroseconds(5);
    digitalWrite(sda, HIGH);
    delayMicroseconds(5);

    pinMode(sda, INPUT_PULLUP);
    pinMode(scl, INPUT_PULLUP);

    SerialUSB.println(digitalRead(sda) == LOW
        ? F("  I2C bus  STILL HELD LOW -- a device needs a power cycle")
        : F("  I2C bus  released"));
}

}  // namespace

void begin() {
    recoverBus();
    Wire.begin();
    Wire.setClock(kI2cClockHz);

    // WAIT FOR THE BUS TO COME ALIVE, rather than delaying a fixed guess.
    //
    // The chain was seen flapping between "all five present" and "all absent"
    // across consecutive identical flashes -- the whole chain together, never
    // a subset. A fixed 50 ms settle fixed it once and then did not, which is
    // what a marginal timing dependency looks like and why a constant is the
    // wrong instrument.
    //
    // So: sweep the address range until SOMETHING answers, up to a budget.
    // Devices that are genuinely absent still cost only the budget once, and a
    // chain that merely wakes slowly is no longer thrown away.
    uint32_t aliveAtMs = 0;
    const uint32_t busDeadline = millis() + 3000;
    while (millis() < busDeadline && aliveAtMs == 0) {
        for (uint8_t addr = 0x08; addr < 0x78 && aliveAtMs == 0; addr++) {
            Wire.beginTransmission(addr);
            if (Wire.endTransmission() == 0) aliveAtMs = millis();
        }
        if (aliveAtMs == 0) delay(25);
    }
    SerialUSB.print(F("  I2C bus  "));
    if (aliveAtMs) {
        SerialUSB.print(F("first device answered after "));
        SerialUSB.print(aliveAtMs - (busDeadline - 3000));
        SerialUSB.println(F(" ms"));
    } else {
        SerialUSB.println(F("NOTHING answered in 3 s -- chain unpowered or unplugged"));
    }
    delay(50);   // a little more grace for the slower devices behind the first
    SerialUSB.print(F("  I2C bus  "));
    SerialUSB.print(kI2cClockHz / 1000);
    SerialUSB.println(F(" kHz"));

    // Field-by-field, not brace-assignment: Device carries default member
    // initialisers, which makes it a non-aggregate before C++14 and there is
    // no matching operator= for a braced list.
    g_dev[kEnv].name = "BME280"; g_dev[kEnv].address = board::kAddrBme280;
    g_dev[kAir].name = "CCS811"; g_dev[kAir].address = board::kAddrCcs811;

    SerialUSB.println(F("-- Qwiic chain ------------------------------------"));
    // Each device announces itself BEFORE it is touched. A library that hangs
    // then leaves its own name as the last line on the console, instead of
    // silence that says only "setup() stopped somewhere".
    SerialUSB.flush();

    // Probe the address before calling begin(). Several SparkFun libraries
    // spin or take seconds to give up when nothing answers, and one absent
    // device must not delay the others.
    SerialUSB.print(F("  BME280   ...")); SerialUSB.flush();
    g_dev[kEnv].present = addressResponds(board::kAddrBme280) && g_bme.beginI2C();
    SerialUSB.print(F("\r"));
    SerialUSB.print(F("  BME280   ")); SerialUSB.println(g_dev[kEnv].present ? F("ok") : F("absent"));

    SerialUSB.print(F("  CCS811   ...")); SerialUSB.flush();
    if (addressResponds(board::kAddrCcs811)) {
        SerialUSB.print(F("\r"));
        const CCS811Core::CCS811_Status_e rc = g_ccs.beginWithStatus();
        g_dev[kAir].present = (rc == CCS811Core::CCS811_Stat_SUCCESS);
        SerialUSB.print(F("  CCS811   ")); SerialUSB.println(g_ccs.statusString(rc));
    } else {
        SerialUSB.print(F("\r"));
        SerialUSB.println(F("  CCS811   absent"));
    }

}

const Device& device(DeviceId id) { return g_dev[id]; }


uint8_t readInto(DeviceId id, char* buf, uint8_t cap) {
    if (!g_dev[id].present) return 0;

    const uint32_t t0 = micros();
    Appender a{buf, cap};
    bool ok = false;

    switch (id) {
        case kEnv: {
            // One burst read of all three measurements, compensated together.
            // Reading them individually costs several I2C transactions AND --
            // worse -- readFloatAltitudeMeters() internally re-reads pressure,
            // so the altitude and pressure in the same 2019 sentence came from
            // two different samples and disagreed by up to 2.4 km.
            BME280_SensorMeasurements m{};
            g_bme.readAllMeasurements(&m, 0);

            // The BME280 returns a fixed invalid pattern when read mid-
            // conversion, and because pressure and humidity are derived from
            // t_fine computed during the temperature read, one bad temperature
            // poisons all three. The 2019 logs show this as -139.22 C in about
            // 8% of samples, always byte-identical. Reject the sample rather
            // than emit it.
            ok = (m.temperature > -40.0f && m.temperature < 85.0f);
            if (ok) {
                g_env.tempC      = m.temperature;
                g_env.humidity   = m.humidity;
                g_env.pressureMb = m.pressure / 100.0f;
                // Altitude from the pressure we already have, so the two
                // always agree. International barometric formula.
                g_env.altitudeM  = 44330.77f * (1.0f - powf(m.pressure / 101325.0f, 0.190263f));
                g_env.fresh      = true;

                a.put("$XXWTH,");
                a.f(g_env.altitudeM,  1, true); a.put(",");
                a.f(g_env.tempC,      2, true); a.put(",");
                a.f(g_env.pressureMb, 2, true); a.put(",");
                a.f(g_env.humidity,   2, true);
            }
            break;
        }

        case kAir: {
            // The CCS811 produces one result per second in its default drive
            // mode. Nothing is wrong when dataAvailable() is false -- it is a
            // gap, not a failure, so it is not counted as one.
            if (!g_ccs.dataAvailable()) {
                record(id, false, micros() - t0);
                return 0;
            }
            g_ccs.readAlgorithmResults();
            g_air.co2   = g_ccs.getCO2();
            g_air.tvoc  = g_ccs.getTVOC();
            g_air.fresh = true;
            ok = true;

            a.put("$XXCO2,");
            a.u(millis());     a.put(",");
            a.u(g_air.co2);    a.put(",");
            a.u(g_air.tvoc);
            break;
        }

        default: return 0;
    }

    const uint32_t dt = micros() - t0;
    if (!ok) { record(id, false, dt); return 0; }

    const uint8_t len = a.finish();
    record(id, len > 0, dt);
    return len;
}
uint8_t formatStats(DeviceId id, char* buf, uint8_t cap) {
    const Device& d = g_dev[id];
    const Stats&  s = d.stats;
    const uint32_t meanUs = s.ok ? static_cast<uint32_t>(s.totalMicros / s.ok) : 0;
    const uint32_t minUs  = (s.minMicros == 0xFFFFFFFF) ? 0 : s.minMicros;

    snprintf(buf, cap, "  %-7s %s  ok %6lu  gap %5lu  streak %3lu/%3lu  us min/mean/max %5lu/%5lu/%5lu",
             d.name,
             d.present ? "     " : "ABSENT",
             static_cast<unsigned long>(s.ok),
             static_cast<unsigned long>(s.failed),
             static_cast<unsigned long>(s.consecutiveFailures),
             static_cast<unsigned long>(s.worstStreak),
             static_cast<unsigned long>(minUs),
             static_cast<unsigned long>(meanUs),
             static_cast<unsigned long>(s.maxMicros));
    return static_cast<uint8_t>(strlen(buf));
}

}  // namespace qwiic
