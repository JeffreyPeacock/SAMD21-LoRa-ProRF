// openlog_test -- exercise the Qwiic OpenLog on its own.
//
// Nothing else on the bus is touched: no sensors, no radio, no scheduler. When
// a device misbehaves inside the full application there are too many things it
// could be, and this removes all of them.
//
// It answers, in order:
//
//   1. Is anything at 0x2A?  And if not, is it anywhere ELSE on the bus? The
//      OpenLog's I2C address is configurable and stored in config.txt ON THE
//      CARD, so a card swap can move it. The ADR jumper forces 0x29.
//   2. What does getStatus() say?  Bit 0 is SD_INIT_GOOD -- that single bit
//      distinguishes "no usable microSD card" from every other failure, and
//      no amount of staring at write timings will tell you the same thing.
//   3. What firmware version is it running?
//   4. Can it create a file, write to it, sync, and report a size back?
//
// Step 4 is the one that matters: a write that returns is not proof of a
// write that landed. Reading the size back afterwards is.

#include <Arduino.h>
#include <Wire.h>
#include <SparkFun_Qwiic_OpenLog_Arduino_Library.h>

#include <board.h>

// File scope, not inside setup(): report() prints it too.
#ifndef QWIIC_I2C_HZ
#define QWIIC_I2C_HZ 400000UL
#endif

namespace {

OpenLog g_log;

// Bit meanings from the library header.
void printStatus(uint8_t st) {
    SerialUSB.print(F("  status 0x"));
    if (st < 0x10) SerialUSB.print('0');
    SerialUSB.println(st, HEX);
    SerialUSB.print(F("    bit0 SD init good      : "));
    SerialUSB.println((st & (1 << 0)) ? F("YES") : F("NO  <-- no usable microSD card"));
    SerialUSB.print(F("    bit1 last cmd success  : "));
    SerialUSB.println((st & (1 << 1)) ? F("yes") : F("no"));
    SerialUSB.print(F("    bit2 last cmd known    : "));
    SerialUSB.println((st & (1 << 2)) ? F("yes") : F("no"));
    SerialUSB.print(F("    bit3 file open         : "));
    SerialUSB.println((st & (1 << 3)) ? F("yes") : F("no"));
    SerialUSB.print(F("    bit4 in root directory : "));
    SerialUSB.println((st & (1 << 4)) ? F("yes") : F("no"));
    if (st == 0x00 || st == 0xFF) {
        SerialUSB.println(F("    0x00/0xFF is not a status -- that is a bus with"));
        SerialUSB.println(F("    nothing answering, not an OpenLog reporting bad news."));
    }
}

uint8_t findOnBus() {
    // 0x2A first, retried: a single probe is not a presence test on this
    // chain -- the first transactions after Wire.begin() fail while later
    // ones succeed.
    for (uint8_t attempt = 0; attempt < 3; attempt++) {
        Wire.beginTransmission(board::kAddrOpenLog);
        if (Wire.endTransmission() == 0) return board::kAddrOpenLog;
        delay(5);
    }

    SerialUSB.println(F("  nothing at 0x2A -- sweeping the whole bus"));
    uint8_t found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() != 0) continue;
        found++;
        SerialUSB.print(F("    0x"));
        if (addr < 0x10) SerialUSB.print('0');
        SerialUSB.print(addr, HEX);
        if (addr == 0x29) SerialUSB.println(F("  <-- OpenLog with the ADR jumper closed"));
        else if (addr == 0x5B) SerialUSB.println(F("  CCS811"));
        else if (addr == 0x77) SerialUSB.println(F("  BME280"));
        else SerialUSB.println(F("  unknown -- could be a relocated OpenLog"));
    }
    if (found == 0) SerialUSB.println(F("    nothing at all -- the bus is dead or unpowered"));
    return 0;
}

void report() {
    digitalWrite(board::kPinStatusLed, HIGH);
    SerialUSB.println();
    SerialUSB.println(F("=================================================="));
    SerialUSB.print(F(" openlog_test -- bus "));
    SerialUSB.print(QWIIC_I2C_HZ / 1000);
    SerialUSB.println(F(" kHz"));
    SerialUSB.println(F("=================================================="));

    SerialUSB.println(F("-- 1. is it on the bus? ----------------------------"));
    const uint8_t addr = findOnBus();
    if (addr == 0) {
        SerialUSB.println();
        SerialUSB.println(F("  The OpenLog is not responding at any address."));
        SerialUSB.println(F("  Check, in this order:"));
        SerialUSB.println(F("    * the red PWR LED. Off = not powered: cable or connector."));
        SerialUSB.println(F("    * the STAT LED. THREE BLINKS = microSD failed to init."));
        SerialUSB.println(F("    * POWER-CYCLE the board. A reflash resets only the SAMD21;"));
        SerialUSB.println(F("      the Qwiic rail never drops, so a wedged device stays"));
        SerialUSB.println(F("      wedged through every upload."));
        SerialUSB.println(F("    * the card: 64MB-32GB, FAT16 or FAT32. Not exFAT."));
        digitalWrite(board::kPinStatusLed, LOW);
        return;
    }
    SerialUSB.print(F("  found at 0x")); SerialUSB.println(addr, HEX);

    SerialUSB.println(F("-- 2. status ---------------------------------------"));
    if (!g_log.begin(addr)) {
        SerialUSB.println(F("  ACKs its address but begin() failed."));
        SerialUSB.println(F("  The ATmega is alive and its firmware is not answering."));
        digitalWrite(board::kPinStatusLed, LOW);
        return;
    }
    printStatus(g_log.getStatus());

    SerialUSB.println(F("-- 3. firmware -------------------------------------"));
    SerialUSB.print(F("  version "));
    SerialUSB.println(g_log.getVersion());

    SerialUSB.println(F("-- 4. write, sync, and read the size back ----------"));
    const char* testFile = "TESTLOG.TXT";
    g_log.append(testFile);
    const uint32_t t0 = micros();
    g_log.println("openlog_test: if you can read this, logging works");
    const uint32_t writeUs = micros() - t0;
    const uint32_t t1 = micros();
    g_log.syncFile();
    const uint32_t syncUs = micros() - t1;

    char buf[96];
    snprintf(buf, sizeof(buf), "  write %lu us, sync %lu us",
             static_cast<unsigned long>(writeUs), static_cast<unsigned long>(syncUs));
    SerialUSB.println(buf);

    // A write that returns is not a write that landed. The size is.
    const int32_t size = g_log.size(testFile);
    snprintf(buf, sizeof(buf), "  %s is %ld bytes", testFile, static_cast<long>(size));
    SerialUSB.println(buf);
    if (size > 0) {
        SerialUSB.println(F("  LOGGING WORKS -- the file exists and has content."));
    } else {
        SerialUSB.println(F("  the write returned but the file is empty or missing."));
        SerialUSB.println(F("  That is a card problem, not a bus problem."));
    }

    printStatus(g_log.getStatus());

    SerialUSB.println(F("-- 5. throughput -----------------------------------"));
    // Batching the node's log made no difference to total time, which means
    // the cost is per BYTE and not per WRITE -- there is no per-transaction
    // overhead to amortise. This measures the actual byte rate, so the
    // question "would a faster bus help?" gets an answer instead of a guess.
    //
    // If throughput scales with bus speed, the I2C transfer is the limit. If
    // it does not, the OpenLog is clock-stretching while it commits to the
    // card, and no bus speed will change that.
    // SAME BYTE COUNT, DIFFERENT NUMBER OF NEWLINES.
    //
    // The node's batched writes measured ~240 ms per RECORD no matter how many
    // records were packed into one I2C transaction -- while this tool measured
    // 512 newline-free bytes in 135 ms. Same device, same bus, 15x apart. The
    // only thing that differs is how many line endings are in the payload.
    //
    // If the OpenLog commits to the card on every newline, then the cost is
    // per RECORD and batching cannot help by construction. This measures it.
    {
        const uint32_t busCapacity = QWIIC_I2C_HZ / 9;
        snprintf(buf, sizeof(buf), "  bus could carry %lu bytes/s at this speed",
                 static_cast<unsigned long>(busCapacity));
        SerialUSB.println(buf);

        char chunk[65];

        // A: 512 bytes, no newlines at all.
        memset(chunk, 'x', 64);
        chunk[64] = '\0';
        uint32_t t0 = micros();
        for (uint8_t i = 0; i < 8; i++) g_log.print(chunk);
        const uint32_t noNewlineUs = micros() - t0;

        // B: 512 bytes, a newline every 64 -- 8 of them.
        memset(chunk, 'y', 63);
        chunk[63] = '\n';
        chunk[64] = '\0';
        t0 = micros();
        for (uint8_t i = 0; i < 8; i++) g_log.print(chunk);
        const uint32_t eightNewlineUs = micros() - t0;

        // C: 512 bytes, a newline every 16 -- 32 of them.
        memset(chunk, 'z', 64);
        for (uint8_t i = 15; i < 64; i += 16) chunk[i] = '\n';
        chunk[64] = '\0';
        t0 = micros();
        for (uint8_t i = 0; i < 8; i++) g_log.print(chunk);
        const uint32_t thirtyTwoNewlineUs = micros() - t0;

        snprintf(buf, sizeof(buf), "  512 B,  0 newlines : %8lu us  (%lu B/s)",
                 static_cast<unsigned long>(noNewlineUs),
                 static_cast<unsigned long>(noNewlineUs ? 512UL * 1000000UL / noNewlineUs : 0));
        SerialUSB.println(buf);
        snprintf(buf, sizeof(buf), "  512 B,  8 newlines : %8lu us",
                 static_cast<unsigned long>(eightNewlineUs));
        SerialUSB.println(buf);
        snprintf(buf, sizeof(buf), "  512 B, 32 newlines : %8lu us",
                 static_cast<unsigned long>(thirtyTwoNewlineUs));
        SerialUSB.println(buf);

        // D: the node's actual shape -- ONE print() of ~560 bytes, which is
        // what a batched flush does. Same byte count as A/B/C, same code path,
        // so if this is slower the difference is transaction size and not
        // content.
        char big[561];
        memset(big, 'w', 560);
        big[560] = '\0';
        t0 = micros();
        g_log.print(big);
        const uint32_t oneBigUs = micros() - t0;
        snprintf(buf, sizeof(buf), "  560 B, ONE print()  : %8lu us  (%lu B/s)",
                 static_cast<unsigned long>(oneBigUs),
                 static_cast<unsigned long>(oneBigUs ? 560UL * 1000000UL / oneBigUs : 0));
        SerialUSB.println(buf);

        // E: THE SAME WRITE, TO THE BIG FILE.
        //
        // Everything above appends to TESTLOG.TXT, a few hundred bytes. The
        // sensor application appends to SENSOR.LOG, which has been growing all
        // session -- and an SD write gets slower as the file grows. Measured
        // on this hardware: 3-5 ms warm, rising to 185-310 ms once the file is
        // large. That is why this measures against BOTH.
        //
        // If this is much slower than D, the cost scales with FILE SIZE, and
        // no amount of batching, bus speed or newline arithmetic will touch it.
        g_log.append("SENSOR.LOG");
        const int32_t bigFileSize = g_log.size("SENSOR.LOG");
        t0 = micros();
        g_log.print(big);
        const uint32_t toBigFileUs = micros() - t0;
        snprintf(buf, sizeof(buf), "  560 B -> SENSOR.LOG (%ld B) : %8lu us  (%lu B/s)",
                 static_cast<long>(bigFileSize),
                 static_cast<unsigned long>(toBigFileUs),
                 static_cast<unsigned long>(toBigFileUs ? 560UL * 1000000UL / toBigFileUs : 0));
        SerialUSB.println(buf);
        if (toBigFileUs > oneBigUs * 3) {
            SerialUSB.println(F("  -> COST SCALES WITH FILE SIZE. Batching, bus speed and"));
            SerialUSB.println(F("     newline count are all irrelevant; the OpenLog is"));
            SerialUSB.println(F("     walking the file every append."));
        }
        g_log.append(testFile);   // put it back

        if (eightNewlineUs > noNewlineUs * 2) {
            SerialUSB.println(F("  -> COST IS PER NEWLINE. The OpenLog commits on each"));
            SerialUSB.println(F("     line ending, so batching records into one I2C"));
            SerialUSB.println(F("     transaction cannot help: the device still sees"));
            SerialUSB.println(F("     the same number of line endings."));
            const uint32_t perNl = (eightNewlineUs - noNewlineUs) / 8;
            snprintf(buf, sizeof(buf), "     roughly %lu us per newline", static_cast<unsigned long>(perNl));
            SerialUSB.println(buf);
        } else {
            SerialUSB.println(F("  -> cost is per BYTE, not per newline."));
        }
    }

    SerialUSB.println(F("-- done -- press any key to re-run -----------------"));
    digitalWrite(board::kPinStatusLed, LOW);
}

}  // namespace

void setup() {
    pinMode(board::kPinStatusLed, OUTPUT);
    SerialUSB.begin(115200);
    const uint32_t deadline = millis() + 10000;
    while (!SerialUSB && millis() < deadline) delay(10);
    // Speed is a knob here on purpose. The OpenLog is an ATmega328 acting as
    // an I2C slave while also driving an SD card over SPI; its TWI hardware
    // ACKs its own address whether or not the firmware is keeping up. So
    // "it ACKs" and "it is answering" are different claims, and a device that
    // ACKs while returning 0xFF to every read is the shape of a slave being
    // clocked faster than its firmware services.
    //
    //     make flash-openlog_test I2CHZ=100000
    Wire.begin();
    Wire.setClock(QWIIC_I2C_HZ);
    delay(50);
    report();
}

void loop() {
    static bool was = false;
    const bool is = static_cast<bool>(SerialUSB);
    if (is && !was) { delay(200); report(); }
    was = is;
    if (SerialUSB.available()) { while (SerialUSB.available()) SerialUSB.read(); report(); }
    delay(50);
}
