// radio_regs -- read back what the SX1276 is ACTUALLY set to, and prove a
// transmission physically happened.
//
// "sent" on the serial console means RadioHead's send() returned. It does not
// mean the frequency is what you asked for, that the PA is enabled, or that
// any RF left the chip. This tool reads the registers back after
// configuration, then transmits and watches the TxDone interrupt flag and the
// operating-mode register, which is as close to "RF came out" as the chip can
// tell us without a spectrum analyser.
//
// Run this when the far end reports "0 received": it separates "we are not
// transmitting what we think" from "the receiver is not hearing us".

#include <Arduino.h>
#include <SPI.h>
#include <RH_RF95.h>

#include <board.h>
#include <rfm95.h>

namespace {

RH_RF95 g_radio(board::kPinRadioCs, board::kPinRadioDio0);
const SPISettings kSpi(1000000, MSBFIRST, SPI_MODE0);

uint8_t rd(uint8_t reg) {
    SPI.beginTransaction(kSpi);
    digitalWrite(board::kPinRadioCs, LOW);
    SPI.transfer(reg & 0x7F);
    const uint8_t v = SPI.transfer(0x00);
    digitalWrite(board::kPinRadioCs, HIGH);
    SPI.endTransaction();
    return v;
}

void hex2(uint8_t v) {
    if (v < 0x10) SerialUSB.print('0');
    SerialUSB.print(v, HEX);
}

void line(const char* name, uint8_t reg, const char* meaning) {
    const uint8_t v = rd(reg);
    SerialUSB.print(F("  0x"));
    hex2(reg);
    SerialUSB.print(F("  "));
    SerialUSB.print(name);
    SerialUSB.print(F(" = 0x"));
    hex2(v);
    SerialUSB.print(F("   "));
    SerialUSB.println(meaning);
}

void dump() {
    SerialUSB.println(F("-- what the chip is actually set to ----------------"));

    line("RegVersion    ", 0x42, "0x12 = SX1276");
    line("RegOpMode     ", 0x01, "bit7 LoRa, bits2-0 mode");

    // Frf is 24 bits, in units of 32 MHz / 2^19 = 61.03515625 Hz.
    const uint32_t frf = (static_cast<uint32_t>(rd(0x06)) << 16) |
                         (static_cast<uint32_t>(rd(0x07)) << 8) |
                          static_cast<uint32_t>(rd(0x08));
    // 61.03515625 Hz = 32e6/2^19. Multiply in 64-bit to avoid overflow.
    const uint64_t hz = (static_cast<uint64_t>(frf) * 32000000ULL) >> 19;
    SerialUSB.print(F("  0x06  RegFrf         = 0x"));
    SerialUSB.print(frf, HEX);
    SerialUSB.print(F("   -> "));
    SerialUSB.print(static_cast<uint32_t>(hz / 1000000ULL));
    SerialUSB.print('.');
    SerialUSB.print(static_cast<uint32_t>((hz % 1000000ULL) / 1000ULL));
    SerialUSB.println(F(" MHz   <-- MUST match the receiver"));

    line("RegPaConfig   ", 0x09, "bit7 PA_BOOST, bits3-0 OutputPower");
    line("RegPaDac      ", 0x4D, "0x84 normal, 0x87 = +20 dBm mode");
    line("RegOcp        ", 0x0B, "overcurrent protection");
    line("RegModemCfg1  ", 0x1D, "0x72 = BW125 CR4/5 explicit header");
    line("RegModemCfg2  ", 0x1E, "0x74 = SF7 CRC-on");
    line("RegModemCfg3  ", 0x26, "bit3 LDRO, bit2 AGC");
    line("RegSyncWord   ", 0x39, "0x12 private, 0x34 LoRaWAN  <-- MUST match");

    const uint16_t pre = (static_cast<uint16_t>(rd(0x20)) << 8) | rd(0x21);
    SerialUSB.print(F("  0x20  RegPreamble    = "));
    SerialUSB.print(pre);
    SerialUSB.println(F(" symbols"));

    // Decode the transmit power the chip will actually use.
    const uint8_t pa = rd(0x09);
    const uint8_t dac = rd(0x4D);
    SerialUSB.print(F("  -> output path : "));
    if (pa & 0x80) {
        const int8_t out = static_cast<int8_t>((pa & 0x0F) + 2 + ((dac == 0x87) ? 3 : 0));
        SerialUSB.print(F("PA_BOOST at "));
        SerialUSB.print(out);
        SerialUSB.println(F(" dBm"));
    } else {
        SerialUSB.println(F("RFO -- WRONG for an RFM95W, RFO is not wired!"));
    }
}

// Transmit and watch the chip, not the library.
void proveTx() {
    SerialUSB.println(F("-- transmitting, watching the chip -----------------"));

    uint8_t msg[] = "REGTEST";
    const uint8_t before = rd(0x01);

    g_radio.send(msg, sizeof(msg));

    // Sample RegOpMode while the packet is in the air. Mode 3 is TX.
    bool sawTx = false;
    const uint32_t deadline = millis() + 1000;
    while (millis() < deadline) {
        if ((rd(0x01) & 0x07) == 0x03) { sawTx = true; break; }
    }
    g_radio.waitPacketSent(1000);

    // RegIrqFlags bit 3 is TxDone. RadioHead clears it in its ISR, so this is
    // best-effort; the mode observation above is the reliable one.
    const uint8_t irq = rd(0x12);

    SerialUSB.print(F("  RegOpMode before = 0x")); hex2(before); SerialUSB.println();
    SerialUSB.print(F("  entered TX mode  : "));
    SerialUSB.println(sawTx ? F("YES -- the PA was keyed") : F("NO  -- nothing was transmitted"));
    SerialUSB.print(F("  RegIrqFlags      = 0x")); hex2(irq);
    SerialUSB.println((irq & 0x08) ? F("   TxDone set") : F("   (TxDone already cleared by the ISR)"));

    if (!sawTx) {
        SerialUSB.println(F("  The chip never entered TX. This is a driver or SPI"));
        SerialUSB.println(F("  problem, not an antenna or frequency problem."));
    }
}

void report() {
    digitalWrite(board::kPinStatusLed, HIGH);
    SerialUSB.println();
    SerialUSB.println(F("=================================================="));
    SerialUSB.println(F(" radio_regs -- SX1276 register readback"));
    SerialUSB.println(F("=================================================="));
    if (!rfm95::begin(g_radio)) {
        SerialUSB.println(F("  radio did not initialise; nothing to read back"));
        digitalWrite(board::kPinStatusLed, LOW);
        return;
    }
    dump();
    proveTx();
    SerialUSB.println(F("-- done -- press any key to re-run ----------------"));
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
