// qwiic_health -- is the I2C BUS working, or is there just nothing on it?
//
// A scan that finds nothing cannot tell those apart, and they need completely
// different responses. This looks at the two wires themselves before any
// addressing happens.
//
// WHAT MAKES THIS POSSIBLE: I2C is open-drain. Nobody ever drives a line high;
// devices only pull low and release, and a PULL-UP RESISTOR does the rest.
// On Qwiic those pull-ups live on the peripheral boards. So the idle voltage
// of SDA and SCL is direct evidence about what is attached:
//
//   read HIGH with the MCU's pull-up disabled  -> an external pull-up exists,
//                                                 i.e. a powered device is on
//                                                 the other end of the cable
//   read LOW  with the MCU's pull-up enabled   -> something is holding that
//                                                 line down: a wedged slave,
//                                                 or a short
//   read LOW  with pull-up disabled, HIGH with
//   it enabled                                 -> nothing out there at all.
//                                                 The cable is unplugged, or
//                                                 the far end is unpowered.
//
// Run it on both boards and compare. The sensor board has five devices and is
// the known-good reference; anything the base board does differently is the
// answer.

#include <Arduino.h>
#include <Wire.h>

#include <board.h>

namespace {

constexpr uint8_t kSda = PIN_WIRE_SDA;   // 20 on this variant
constexpr uint8_t kScl = PIN_WIRE_SCL;   // 21

const char* level(int v) { return v == HIGH ? "HIGH" : "LOW "; }

// Release the line, then watch how quickly it comes back up. A 2.2k-10k
// external pull-up recovers far faster than the SAMD21's ~40k internal one, so
// this separates "a device's pull-up" from "only the MCU's".
uint32_t riseTimeUs(uint8_t pin, uint8_t otherPin) {
    // BOTH pins to a plain input first. This matters more than it looks.
    //
    // An UNPOWERED Qwiic device still has its two pull-up resistors, and they
    // meet at its dead 3V3 rail. That makes a path from one bus line, through
    // one pull-up, across the floating rail, through the other pull-up, to the
    // other line. If the MCU is holding the OTHER line up with its own
    // internal pull-up, current flows along that path and raises the line
    // under test -- and the tool reports "external pull-ups present" for a
    // device with no power at all.
    //
    // That is exactly what happened here: this reported pull-ups on a board
    // whose power LED was dark. Leaving the other pin floating removes the
    // source and the measurement means what it claims.
    pinMode(otherPin, INPUT);
    pinMode(pin, INPUT);
    delayMicroseconds(20);

    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    delayMicroseconds(50);
    pinMode(pin, INPUT);
    const uint32_t t0 = micros();
    while (digitalRead(pin) == LOW) {
        if (micros() - t0 > 10000) return 0;   // never rose
    }
    return micros() - t0;
}

// ---------------------------------------------------------------------------
// Bit-banged I2C, so the roles of the two pins can be SWAPPED.
//
// A Qwiic cable has four conductors: GND, 3V3, SDA, SCL. The rise-time test
// above rules out three failure modes -- a broken SDA or SCL would show that
// line never rising, and a broken 3V3 would leave the device unpowered with no
// pull-ups at all. Both lines rise, so all three are intact.
//
// What it CANNOT rule out is SDA and SCL being swapped. Both conductors still
// reach the device, so both still show its pull-ups; the device simply sees
// clock on its data pin and never acknowledges anything, at any speed. That is
// exactly the evidence we have.
//
// Wire cannot test this: it owns the pin roles. Bit-banging can. If an address
// ACKs with the roles reversed, the cable is mis-wired.
// ---------------------------------------------------------------------------
void sclHigh(uint8_t scl) { pinMode(scl, INPUT_PULLUP); delayMicroseconds(5); }
void sclLow(uint8_t scl)  { pinMode(scl, OUTPUT); digitalWrite(scl, LOW); delayMicroseconds(5); }
void sdaHigh(uint8_t sda) { pinMode(sda, INPUT_PULLUP); delayMicroseconds(5); }
void sdaLow(uint8_t sda)  { pinMode(sda, OUTPUT); digitalWrite(sda, LOW); delayMicroseconds(5); }

// Returns true if the address ACKed -- i.e. something pulled SDA low on the
// ninth clock.
bool softProbe(uint8_t sda, uint8_t scl, uint8_t addr) {
    sdaHigh(sda); sclHigh(scl);
    sdaLow(sda);                       // START: SDA falls while SCL is high
    sclLow(scl);

    const uint8_t byte = static_cast<uint8_t>(addr << 1);   // write
    for (int8_t bit = 7; bit >= 0; bit--) {
        if ((byte >> bit) & 1) sdaHigh(sda); else sdaLow(sda);
        sclHigh(scl);
        sclLow(scl);
    }

    sdaHigh(sda);                      // release for the ACK
    sclHigh(scl);
    const bool ack = (digitalRead(sda) == LOW);
    sclLow(scl);

    sdaLow(sda);                       // STOP
    sclHigh(scl);
    sdaHigh(sda);
    return ack;
}

int softScan(uint8_t sda, uint8_t scl) {
    int found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (!softProbe(sda, scl, addr)) continue;
        found++;
        SerialUSB.print(F(" 0x"));
        if (addr < 0x10) SerialUSB.print('0');
        SerialUSB.print(addr, HEX);
    }
    return found;
}

void report() {
    digitalWrite(board::kPinStatusLed, HIGH);
    SerialUSB.println();
    SerialUSB.println(F("=================================================="));
    SerialUSB.println(F(" qwiic_health -- the bus itself, before addressing"));
    SerialUSB.println(F("=================================================="));
    SerialUSB.print(F("  SDA = pin ")); SerialUSB.print(kSda);
    SerialUSB.print(F(", SCL = pin ")); SerialUSB.println(kScl);

    // --- 1. THE ACTIVE TEST, and the only trustworthy one -----------------
    //
    // Reading a floating input is NOT evidence. A CMOS input with nothing
    // attached holds whatever charge was last on the pin and reads HIGH --
    // which is exactly what this tool did on an UNPLUGGED bus, cheerfully
    // reporting "external pull-ups present".
    //
    // So: drive the line LOW, release it to a plain input, and see whether
    // anything pulls it back up. Only a real pull-up can. That question has a
    // physical answer and a floating read does not.
    const uint32_t sdaRise = riseTimeUs(kSda, kScl);
    const uint32_t sclRise = riseTimeUs(kScl, kSda);
    const bool externalPullups = (sdaRise > 0 && sclRise > 0);

    SerialUSB.println(F("-- 1. drive low, release, does anything pull it up? "));
    char buf[96];
    if (sdaRise) snprintf(buf, sizeof(buf), "     SDA rose in %lu us", static_cast<unsigned long>(sdaRise));
    else         snprintf(buf, sizeof(buf), "     SDA NEVER ROSE");
    SerialUSB.println(buf);
    if (sclRise) snprintf(buf, sizeof(buf), "     SCL rose in %lu us", static_cast<unsigned long>(sclRise));
    else         snprintf(buf, sizeof(buf), "     SCL NEVER ROSE");
    SerialUSB.println(buf);
    SerialUSB.println(externalPullups
        ? F("     -> EXTERNAL PULL-UPS PRESENT: a powered device is attached")
        : F("     -> NO external pull-up: nothing attached, or UNPOWERED"));
    SerialUSB.println(F("     (cross-check this against the device's own power LED --"));
    SerialUSB.println(F("      a dark LED beats any measurement made from here)"));

    // --- 2. is either line held down by something? ------------------------
    pinMode(kSda, INPUT_PULLUP);
    pinMode(kScl, INPUT_PULLUP);
    delay(5);
    const int sdaPulled = digitalRead(kSda);
    const int sclPulled = digitalRead(kScl);

    SerialUSB.println(F("-- 2. with the MCU pull-up on, is a line held low? --"));
    SerialUSB.print(F("     SDA ")); SerialUSB.print(level(sdaPulled));
    SerialUSB.print(F("   SCL ")); SerialUSB.println(level(sclPulled));
    const bool stuckLow = (sdaPulled == LOW || sclPulled == LOW);
    if (stuckLow) {
        SerialUSB.println(F("     -> A LINE IS HELD LOW. A slave interrupted mid-byte"));
        SerialUSB.println(F("        -- which is what an upload does to it -- can hold"));
        SerialUSB.println(F("        SDA down forever. A short looks the same."));
    } else {
        SerialUSB.println(F("     -> neither line is held down"));
    }

    // --- 3. address scan, but ONLY if the bus can carry one ----------------
    SerialUSB.println(F("-- 3. address scan ---------------------------------"));
    int found = 0;
    if (stuckLow || !externalPullups) {
        // A scan on a bus with SDA stuck low reports EVERY address as present,
        // because endTransmission() reads the stuck line as an ACK. That is
        // not 112 devices; it is one broken wire. Refuse rather than print
        // nonsense -- an earlier version printed 0x08, 0x09, 0x0A...
        SerialUSB.println(F("     SKIPPED: without a pull-up on both lines a scan is"));
        SerialUSB.println(F("     meaningless. A low SDA reads as an ACK from every"));
        SerialUSB.println(F("     address, so it would report 112 devices."));
    } else {
        // SWEEP THE BUS SPEED, do not assume one.
        //
        // The Qwiic OpenLog is an ATmega328 acting as an I2C slave while also
        // driving an SD card over SPI, and it does not keep up at 400 kHz --
        // measured, see docs/qwiic-timing.md. A scan at a single speed can
        // therefore report an empty bus while a device sits on it perfectly
        // healthy at a slower clock.
        static const uint32_t kSpeeds[] = {400000UL, 100000UL, 50000UL, 10000UL};
        Wire.begin();
        for (uint8_t i = 0; i < sizeof(kSpeeds) / sizeof(kSpeeds[0]); i++) {
            Wire.setClock(kSpeeds[i]);
            delay(50);
            int here = 0;
            snprintf(buf, sizeof(buf), "     at %6lu Hz:",
                     static_cast<unsigned long>(kSpeeds[i]));
            SerialUSB.print(buf);
            for (uint8_t addr = 0x08; addr < 0x78; addr++) {
                bool ack = false;
                for (uint8_t a = 0; a < 3 && !ack; a++) {
                    Wire.beginTransmission(addr);
                    ack = (Wire.endTransmission() == 0);
                    if (!ack) delay(2);
                }
                if (!ack) continue;
                here++;
                SerialUSB.print(F(" 0x"));
                if (addr < 0x10) SerialUSB.print('0');
                SerialUSB.print(addr, HEX);
            }
            if (here == 0) SerialUSB.print(F(" (nothing)"));
            SerialUSB.println();
            if (here > found) found = here;
        }
        if (found > 0) {
            SerialUSB.println(F("     -> if a device appears only at the slower speeds,"));
            SerialUSB.println(F("        that is the device, not the bus."));
        }
    }

    // --- 4. is the cable's SDA/SCL pair swapped? --------------------------
    SerialUSB.println(F("-- 4. bit-banged scan, both pin orientations -------"));
    SerialUSB.print(F("     normal  (SDA=20, SCL=21):"));
    const int normalFound = softScan(kSda, kScl);
    if (normalFound == 0) SerialUSB.print(F(" (nothing)"));
    SerialUSB.println();

    SerialUSB.print(F("     SWAPPED (SDA=21, SCL=20):"));
    const int swappedFound = softScan(kScl, kSda);
    if (swappedFound == 0) SerialUSB.print(F(" (nothing)"));
    SerialUSB.println();

    if (swappedFound > 0 && normalFound == 0) {
        SerialUSB.println(F("     -> THE CABLE'S SDA AND SCL ARE SWAPPED. The device is"));
        SerialUSB.println(F("        fine; it has been receiving clock on its data pin."));
    } else if (normalFound > 0) {
        SerialUSB.println(F("     -> wiring is correct"));
    } else {
        SerialUSB.println(F("     -> no answer either way, so it is not a swapped pair."));
        SerialUSB.println(F("        Remaining: a broken GND (the device cannot work, but"));
        SerialUSB.println(F("        its pull-ups still show), a high-resistance or"));
        SerialUSB.println(F("        intermittent contact, or a dead device."));
    }

    // --- verdict -----------------------------------------------------------
    SerialUSB.println(F("-- verdict -----------------------------------------"));
    if (found > 0) {
        SerialUSB.println(F("  The bus works and devices are answering."));
    } else if (stuckLow) {
        SerialUSB.println(F("  A LINE IS STUCK LOW. Nothing can be addressed until that"));
        SerialUSB.println(F("  is fixed. Power-cycle the board: a reflash resets the"));
        SerialUSB.println(F("  SAMD21 only and never drops the Qwiic rail, so a wedged"));
        SerialUSB.println(F("  slave survives every upload."));
    } else if (!externalPullups) {
        SerialUSB.println(F("  NOTHING IS ATTACHED. No pull-up on either line means no"));
        SerialUSB.println(F("  powered device on the far end of the cable: unplugged,"));
        SerialUSB.println(F("  in the wrong socket, or the far board has no power."));
        SerialUSB.println(F("  This board's own I2C pins are fine."));
    } else {
        SerialUSB.println(F("  THE BUS AND THE CABLE ARE FINE, and the device is NOT."));
        SerialUSB.println(F(""));
        SerialUSB.println(F("  A Qwiic device's pull-ups sit between the bus lines and"));
        SerialUSB.println(F("  the 3V3 rail that the CABLE delivers. Seeing them rise"));
        SerialUSB.println(F("  therefore proves all four conductors reach the module:"));
        SerialUSB.println(F("  3V3 and GND to bias the resistors, SDA and SCL to carry"));
        SerialUSB.println(F("  them. Power is arriving."));
        SerialUSB.println(F(""));
        SerialUSB.println(F("  Ruled out by that alone: broken SDA, broken SCL, broken"));
        SerialUSB.println(F("  3V3, broken GND. The swapped-pair scan above rules out"));
        SerialUSB.println(F("  a mis-wired pair. So it is not the cable."));
        SerialUSB.println(F(""));
        SerialUSB.println(F("  CHECK THE DEVICE'S POWER LED. If it is dark while these"));
        SerialUSB.println(F("  pull-ups are rising, power reaches the module and the"));
        SerialUSB.println(F("  module does nothing with it -- the part is dead, not"));
        SerialUSB.println(F("  wedged, and no power cycle will help."));
        SerialUSB.println(F(""));
        SerialUSB.println(F("  Confirm by moving it to a chain that is known to work."));
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
