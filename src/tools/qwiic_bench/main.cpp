// qwiic_bench -- benchmark ONE Qwiic device at a time.
//
//     make flash-qwiic_bench DEV=bme280 | ccs811 | openlog
//
// Built after trying to bring all five up at once and spending an evening on
// it. One device, on an otherwise idle bus, answers questions that a full
// cycle cannot:
//
//   * how long does a read of this device actually take, best/typical/worst
//   * how much rest does the bus need before the NEXT transaction succeeds
//   * does the device have a minimum interval below which it simply fails
//
// The inter-transaction spacing test is the point. It walks the gap between
// consecutive transactions down from 20 ms to 0 and reports the failure rate
// at each step, which is what actually determines whether a multi-device cycle
// can work and how long a slot has to be.
//
// NOTHING HERE CALLS A LIBRARY begin() THAT CAN BLOCK. Sensor libraries are
// entitled to spin waiting for a device to answer, and a benchmark that hangs
// measures nothing. Everything below talks raw Wire with explicit limits.

#include <Arduino.h>
#include <Wire.h>

#include <board.h>

namespace {

#ifndef BENCH_ADDR
#define BENCH_ADDR 0x77
#define BENCH_NAME "BME280"
#define BENCH_REG  0xD0      // chip id
#define BENCH_LEN  1
#endif

constexpr uint8_t  kAddr = BENCH_ADDR;
#ifndef BENCH_REG
#define BENCH_REG 0x00
#endif
constexpr uint8_t  kReg  = BENCH_REG;
constexpr uint8_t  kLen  = BENCH_LEN;

#ifndef QWIIC_I2C_HZ
#define QWIIC_I2C_HZ 400000UL
#endif

// One transaction: point at a register, read kLen bytes back. This is the
// smallest unit of I2C work every device here has in common, so timings are
// comparable across devices rather than measuring library overhead.
bool transact(uint32_t& elapsedUs) {
    const uint32_t t0 = micros();
#ifndef BENCH_NO_REGISTER
    // Register-addressed device: point at a register, repeated start, read.
    Wire.beginTransmission(kAddr);
    Wire.write(kReg);
    if (Wire.endTransmission(false) != 0) { elapsedUs = micros() - t0; return false; }
#endif
    // BENCH_NO_REGISTER: a plain read with no register write in front of it.
    // Not every Qwiic device is register-addressed -- a streaming device just
    // hands over its next bytes, and writing a register byte at one produces
    // 100% NAK and measures nothing about the bus.
    const uint8_t got = Wire.requestFrom(kAddr, kLen);
    while (Wire.available()) Wire.read();
    elapsedUs = micros() - t0;
    return got == kLen;
}

struct Result {
    uint32_t ok = 0, fail = 0;
    uint32_t minUs = 0xFFFFFFFF, maxUs = 0;
    uint64_t totalUs = 0;
};

Result run(uint16_t iterations, uint32_t gapUs) {
    Result r;
    for (uint16_t i = 0; i < iterations; i++) {
        uint32_t us = 0;
        if (transact(us)) {
            r.ok++;
            if (us < r.minUs) r.minUs = us;
            if (us > r.maxUs) r.maxUs = us;
            r.totalUs += us;
        } else {
            r.fail++;
        }
        if (gapUs) delayMicroseconds(gapUs);
    }
    return r;
}

void line(const char* what, const Result& r, uint16_t n) {
    char buf[128];
    // Integer tenths of a percent, NOT %f. The Arduino SAMD core links
    // newlib-nano, whose printf has no floating-point support: a %f emits
    // nothing at all. lib/nmea exists because of exactly this, and the first
    // version of this line still fell into it -- it printed "fail 0 (%)".
    const uint32_t failTenths = n ? (static_cast<uint32_t>(r.fail) * 1000u / n) : 0u;
    snprintf(buf, sizeof(buf),
             "  %-14s ok %4lu/%4u  fail %4lu (%2lu.%01lu%%)  us min/mean/max %5lu/%5lu/%5lu",
             what,
             static_cast<unsigned long>(r.ok), n,
             static_cast<unsigned long>(r.fail),
             static_cast<unsigned long>(failTenths / 10),
             static_cast<unsigned long>(failTenths % 10),
             static_cast<unsigned long>(r.minUs == 0xFFFFFFFF ? 0 : r.minUs),
             static_cast<unsigned long>(r.ok ? r.totalUs / r.ok : 0),
             static_cast<unsigned long>(r.maxUs));
    SerialUSB.println(buf);
}

void report() {
    digitalWrite(board::kPinStatusLed, HIGH);
    SerialUSB.println();
    SerialUSB.println(F("=================================================="));
    SerialUSB.print(F(" qwiic_bench -- "));
    SerialUSB.print(F(BENCH_NAME));
    SerialUSB.print(F(" at 0x"));
    SerialUSB.print(kAddr, HEX);
    SerialUSB.print(F(", bus "));
    SerialUSB.print(QWIIC_I2C_HZ / 1000);
    SerialUSB.println(F(" kHz"));
    SerialUSB.println(F("=================================================="));

    Wire.beginTransmission(kAddr);
    if (Wire.endTransmission() != 0) {
        SerialUSB.println(F("  device does not ACK its address -- nothing to measure"));
        digitalWrite(board::kPinStatusLed, LOW);
        return;
    }

    SerialUSB.println(F("-- transaction cost, bus otherwise idle ------------"));
    line("warmup", run(20, 5000), 20);
    line("100 reads", run(100, 5000), 100);

    SerialUSB.println(F("-- how much rest does the bus need? ----------------"));
    SerialUSB.println(F("   gap = delay between the end of one transaction"));
    SerialUSB.println(F("   and the start of the next."));
    static const uint32_t kGapsUs[] = {20000, 10000, 5000, 2000, 1000, 500, 200, 100, 50, 0};
    for (uint8_t i = 0; i < sizeof(kGapsUs) / sizeof(kGapsUs[0]); i++) {
        char what[16];
        const uint32_t g = kGapsUs[i];
        if (g >= 1000) snprintf(what, sizeof(what), "gap %lums", static_cast<unsigned long>(g / 1000));
        else           snprintf(what, sizeof(what), "gap %luus", static_cast<unsigned long>(g));
        line(what, run(100, g), 100);
    }

    SerialUSB.println(F("-- done -- press any key to re-run ----------------"));
    digitalWrite(board::kPinStatusLed, LOW);
}

}  // namespace

void setup() {
    pinMode(board::kPinStatusLed, OUTPUT);
    SerialUSB.begin(115200);
    const uint32_t deadline = millis() + 10000;
    while (!SerialUSB && millis() < deadline) delay(10);
    Wire.begin();
    Wire.setClock(QWIIC_I2C_HZ);
    report();
}

void loop() {
    static bool wasConnected = false;
    const bool isConnected = static_cast<bool>(SerialUSB);
    if (isConnected && !wasConnected) { delay(200); report(); }
    wasConnected = isConnected;
    if (SerialUSB.available()) {
        while (SerialUSB.available()) SerialUSB.read();
        report();
    }
    delay(50);
}
