#include "datalog.h"

#include <Arduino.h>
#include <Wire.h>
#include <SparkFun_Qwiic_OpenLog_Arduino_Library.h>
#include <WString.h>

#include <board.h>

namespace datalog {

namespace {
// "255.x" or "x.255" both mean at least one byte came back 0xFF, and an empty
// string means nothing came back at all.
bool looksLikeGarbage(const String& v) {
    return v.length() == 0 || v.startsWith("255") || v.endsWith("255");
}
}  // namespace

namespace {

OpenLog g_log;
bool    g_present = false;

// THE OPENLOG CANNOT KEEP UP AT 400 kHz, and it fails by lying rather than by
// erroring. Measured 2026-08-23 with src/tools/openlog_test:
//
//     bus      getStatus()   getVersion()   bytes written
//     400 kHz  0xFF          255.255        0
//     100 kHz  0xFF          2.0            206
//      50 kHz  0x1D          2.0            380   <- correct
//
// 0xFF is not a status. It is an idle bus read back through a pull-up. The
// device is an ATmega328 acting as an I2C slave WHILE driving an SD card over
// SPI: its TWI hardware ACKs the address by itself, so the device looks
// present at any speed, and only the payload is wrong. That is why this
// presented as "the OpenLog is absent" and as writes that returned
// successfully having written nothing.
//
// The sensors want 400 kHz -- the CCS811 and BME280 are happy there and a
// 50 ms rotation vector. So the speed is changed around OpenLog transactions
// and put back afterwards. Wire.setClock() only rewrites a baud register; it
// costs nothing measurable next to the milliseconds an SD write takes.
constexpr uint32_t kOpenLogHz = 50000UL;

#ifndef QWIIC_I2C_HZ
#define QWIIC_I2C_HZ 400000UL
#endif

struct SlowBus {
    SlowBus()  { Wire.setClock(kOpenLogHz); }
    ~SlowBus() { Wire.setClock(QWIIC_I2C_HZ); }
};

}  // namespace

bool begin(const char* filename) {
#ifdef DATALOG_DISABLE
    // -DDATALOG_DISABLE builds without logging, for isolating a hang. The
    // OpenLog ACKs its address from its ATmega whether or not a usable microSD
    // card is present, so "the device answered" is NOT evidence that a write
    // will return.
    SerialUSB.println(F("  OpenLog  DISABLED at build time"));
    g_present = false;
    return false;
#endif
    // Retried, for the same reason lib/qwiic retries: a single probe is not a
    // reliable presence test on this chain. The first transactions after
    // Wire.begin() fail while later ones succeed, and a one-shot probe
    // therefore drops a device that is working perfectly well -- which is
    // exactly what happened here, with the OpenLog reported absent in the node
    // while an i2c_scan found it every time.
    SlowBus slow;

    bool answered = false;
    for (uint8_t attempt = 0; attempt < 3 && !answered; attempt++) {
        Wire.beginTransmission(board::kAddrOpenLog);
        answered = (Wire.endTransmission() == 0);
        if (!answered) delay(5);
    }
    if (!answered) {
        SerialUSB.println(F("  OpenLog  no response at 0x2A"));
        g_present = false;
        return false;
    }
    if (!g_log.begin(board::kAddrOpenLog)) {
        SerialUSB.println(F("  OpenLog  answered 0x2A but begin() failed"));
        g_present = false;
        return false;
    }
    // append() creates the file if it does not exist and seeks to the end if
    // it does, so a power cycle adds to the log rather than erasing it.
    g_log.append(filename);

    // VERIFY, rather than assume. getVersion() returning "255.255" means every
    // read is coming back 0xFF and nothing is really answering -- which is
    // exactly what 400 kHz produced while still ACKing its address.
    // getVersion() IS NOT A RELIABLE GATE, and gating on it produced a false
    // negative that cost a hardware hunt. Measured 2026-08-25 on one device,
    // one bus speed, minutes apart:
    //
    //   openlog_test at 50 kHz   version 2.255   then wrote and read back
    //                                            1752 bytes -- LOGGING WORKS
    //   this code at 50 kHz      version 255.0   declared ABSENT
    //
    // Both reads are partly corrupt: one byte is 0xFF either way. The read is
    // simply unreliable on this device, so a single sample of it decides
    // nothing -- and the version check was rejecting an OpenLog that logs.
    //
    // Retry first, because the fault is intermittent rather than permanent.
    String version = g_log.getVersion();
    for (uint8_t attempt = 0; attempt < 3 && looksLikeGarbage(version); attempt++) {
        delay(20);
        version = g_log.getVersion();
    }
    if (looksLikeGarbage(version)) {
        // Still garbage. That is NOT enough to condemn it -- ask the only
        // question that matters instead: does a write land? openlog_test
        // settles it this way and it is the difference between an ID read and
        // a functional test.
        SerialUSB.print(F("  OpenLog  version reads garbage ("));
        SerialUSB.print(version);
        SerialUSB.print(F(") -- testing a write: "));
        const int32_t before = g_log.size(filename);
        g_log.println("$PJPL,probe");
        g_log.syncFile();
        const int32_t after = g_log.size(filename);
        if (after > before) {
            SerialUSB.println(F("WRITE LANDED, using it"));
            g_present = true;
            return true;
        }
        SerialUSB.println(F("write did not land -- not usable"));
        g_present = false;
        return false;
    }

    const uint8_t status = g_log.getStatus();
    if (status == 0xFF || !(status & 0x01)) {
        SerialUSB.print(F("  OpenLog  status 0x"));
        SerialUSB.print(status, HEX);
        SerialUSB.println(status == 0xFF ? F(" -- not answering")
                                         : F(" -- microSD did not initialise"));
        g_present = false;
        return false;
    }

    SerialUSB.print(F("  OpenLog  firmware "));
    SerialUSB.print(version);
    SerialUSB.print(F(", SD ok, "));
    SerialUSB.print(kOpenLogHz / 1000);
    SerialUSB.println(F(" kHz"));
    g_present = true;
    return true;
}

bool present() { return g_present; }

void line(const char* s) {
    if (!g_present) return;
    SlowBus slow;
    g_log.println(s);
}

void sync() {
    if (!g_present) return;
    SlowBus slow;
    g_log.syncFile();
}

}  // namespace datalog
