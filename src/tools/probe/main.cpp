// probe -- prove the hardware before believing anything else.
//
// Radio and Qwiic bus together, so one run answers "is this board healthy?".
// The two halves also exist separately: `i2c_scan` is this file's bus scan
// with no radio and no libraries at all.
//
// Three questions:
//
//   1. Is the RFM95W where board.h says it is?  Read RegVersion (0x42). The
//      SX1276 returns a fixed 0x12. A bus with nothing driving it returns 0x00
//      or 0xFF, and both are unambiguous.
//   2. Does RST on pin 7 actually reset the part, or is it just a number from
//      a tutorial?  The sibling ESP32 gateway project found that BOTH reset
//      pins SparkFun publishes for that board are unwired.
//   3. What is on the Qwiic chain?
//
// Nothing here transmits, so it is safe with no antenna attached.

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#include <board.h>

namespace {

constexpr uint8_t kRegOpMode  = 0x01;
constexpr uint8_t kRegVersion = 0x42;

// SX1276 SPI: MSB of the address byte is the write bit. Mode 0, MSB first.
// 10 MHz is well inside the part's 10 MHz limit and the SAMD21's capability.
const SPISettings kSpi(10000000, MSBFIRST, SPI_MODE0);

uint8_t radioRead(uint8_t reg) {
    SPI.beginTransaction(kSpi);
    digitalWrite(board::kPinRadioCs, LOW);
    SPI.transfer(reg & 0x7F);
    const uint8_t value = SPI.transfer(0x00);
    digitalWrite(board::kPinRadioCs, HIGH);
    SPI.endTransaction();
    return value;
}

void radioWrite(uint8_t reg, uint8_t value) {
    SPI.beginTransaction(kSpi);
    digitalWrite(board::kPinRadioCs, LOW);
    SPI.transfer(reg | 0x80);
    SPI.transfer(value);
    digitalWrite(board::kPinRadioCs, HIGH);
    SPI.endTransaction();
}

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

void probeRadio() {
    SerialUSB.println(F("-- radio ------------------------------------------"));

    pinMode(board::kPinRadioCs, OUTPUT);
    digitalWrite(board::kPinRadioCs, HIGH);
    pinMode(board::kPinRadioDio0, INPUT);
    SPI.begin();

    // Hardware reset: RST is active low and the SX1276 wants 100 us low and
    // 5 ms to settle. Unlike the ESP32 gateway next door, this board really
    // does have a reset line -- pin 7 -- and the test below proves it.
    pinMode(board::kPinRadioRst, OUTPUT);
    digitalWrite(board::kPinRadioRst, LOW);
    delay(10);
    digitalWrite(board::kPinRadioRst, HIGH);
    delay(10);

    const uint8_t version = radioRead(kRegVersion);
    SerialUSB.print(F("  RegVersion (0x42) = 0x"));
    if (version < 0x10) SerialUSB.print('0');
    SerialUSB.print(version, HEX);
    if (version == 0x12) {
        SerialUSB.println(F("   SX1276 FOUND"));
    } else if (version == 0x00 || version == 0xFF) {
        SerialUSB.println(F("   NOTHING ON THE BUS -- check CS/SPI wiring"));
        return;
    } else {
        SerialUSB.println(F("   answered, but this is not an SX1276"));
    }

    // Does RST actually reset the part?  Put RegOpMode somewhere it cannot be
    // after a reset, pulse RST, read it back. The reset value is 0x09 --
    // FSK/OOK, STDBY, LowFrequencyModeOn.
    //
    // The value written is 0x00 (FSK, SLEEP), NOT 0x80 (LoRa, SLEEP). Bit 7
    // LongRangeMode "can only be modified in Sleep mode" per the SX1276
    // datasheet 6.4, so a single write of 0x80 from the post-reset STDBY state
    // does not reliably latch -- the read-back would be 0x00 anyway and the
    // printed "before" value would be a lie. 0x00 is legal from any state.
    radioWrite(kRegOpMode, 0x00);
    const uint8_t before = radioRead(kRegOpMode);

    // Prove the write took effect BEFORE drawing any conclusion from the
    // reset. Without this check, an SPI bus that reads fine but cannot write
    // leaves RegOpMode at 0x09 both times, and the test blames the reset pin
    // for a fault that is nothing to do with it.
    if (before != 0x00) {
        SerialUSB.print(F("  RegOpMode write did not take: wrote 0x00, read 0x"));
        SerialUSB.println(before, HEX);
        SerialUSB.println(F("  Cannot test RST -- reads work, writes do not."));
        return;
    }

    digitalWrite(board::kPinRadioRst, LOW);
    delayMicroseconds(200);   // datasheet 7.2.2: RST low for >100 us
    digitalWrite(board::kPinRadioRst, HIGH);
    delay(10);                // then 5 ms before the chip is usable
    const uint8_t after = radioRead(kRegOpMode);

    SerialUSB.print(F("  RegOpMode 0x00 written, after reset = 0x"));
    SerialUSB.print(after, HEX);
    if (after == 0x09) {
        SerialUSB.println(F("   RST on pin 7 WORKS (0x09 is the reset value)"));
    } else if (after == 0x00) {
        SerialUSB.println(F("   RST on pin 7 DID NOTHING -- register unchanged"));
    } else {
        SerialUSB.println(F("   unexpected; the part changed but not to 0x09"));
    }
}

void probeQwiic() {
    SerialUSB.println(F("-- Qwiic / I2C ------------------------------------"));

    Wire.begin();
    Wire.setClock(board::kI2cClockHz);

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
        SerialUSB.println(F("  A Qwiic chain is a shared bus: one unpowered or"));
        SerialUSB.println(F("  reversed link takes out everything past it."));
    } else {
        SerialUSB.print(F("  "));
        SerialUSB.print(found);
        SerialUSB.println(F(" device(s)."));
    }
}

void report() {
    digitalWrite(board::kPinStatusLed, HIGH);
    SerialUSB.println();
    SerialUSB.println(F("=================================================="));
    SerialUSB.println(F(" SparkFun Pro RF (SAMD21) -- hardware probe"));
    SerialUSB.println(F("=================================================="));
    probeRadio();
    probeQwiic();
    SerialUSB.println(F("-- done -- press any key to re-run ----------------"));
    digitalWrite(board::kPinStatusLed, LOW);
}

}  // namespace

void setup() {
    pinMode(board::kPinStatusLed, OUTPUT);
    digitalWrite(board::kPinStatusLed, HIGH);

    SerialUSB.begin(115200);
    // Wait for a host, but never forever: a board that only runs when someone
    // is watching is a board that cannot be left on a bench. loop() reprints
    // whenever a host attaches later, so nothing is lost by giving up here.
    const uint32_t deadline = millis() + 10000;
    while (!SerialUSB && millis() < deadline) delay(10);

    report();
}

void loop() {
    // Re-run when a host ATTACHES, not only at boot.
    //
    // This board resets itself the moment an upload finishes, so setup() runs
    // and prints while nothing is listening -- the Arduino SAMD core discards
    // CDC writes with no host attached, and the output is simply gone. A
    // monitor started even a second later sees an empty screen and the board
    // looks dead. Watching for the false->true edge on SerialUSB fixes that:
    // whenever a terminal opens the port, the results are reprinted.
    static bool wasConnected = false;
    const bool isConnected = static_cast<bool>(SerialUSB);

    if (isConnected && !wasConnected) {
        delay(200);   // let the host settle before writing into it
        report();
    }
    wasConnected = isConnected;

    // Also re-run on any keypress, so a cable can be moved and the board
    // re-probed without cycling USB and losing the port.
    if (SerialUSB.available()) {
        while (SerialUSB.available()) SerialUSB.read();
        report();
    }
    delay(50);
}
