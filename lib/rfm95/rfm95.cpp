#include "rfm95.h"

#include <Arduino.h>
#include <SPI.h>

#include <board.h>
#include <nmea.h>

namespace rfm95 {

void reset() {
    pinMode(board::kPinRadioRst, OUTPUT);
    digitalWrite(board::kPinRadioRst, LOW);
    delayMicroseconds(200);   // datasheet 7.2.2: >100 us
    digitalWrite(board::kPinRadioRst, HIGH);
    delay(10);                // then ~5 ms before the part is usable
}

bool begin(RH_RF95& radio, uint8_t txPowerDbm) {
    reset();

    if (!radio.init()) {
        SerialUSB.println(F("  RFM95W   FAILED -- init() did not answer."));
        SerialUSB.println(F("           Run the `probe` tool: it reads RegVersion"));
        SerialUSB.println(F("           and says whether the SPI bus is alive."));
        return false;
    }
    if (!radio.setFrequency(board::kDefaultFrequencyMhz)) {
        SerialUSB.println(F("  RFM95W   FAILED -- setFrequency() rejected the value."));
        return false;
    }

    // The modem config is left at RH_RF95's default, Bw125Cr45Sf128 -- which
    // init() applies itself rather than leaving to the chip. BW 125 kHz, SF7,
    // CR 4/5, explicit header, CRC on, 8-symbol preamble, sync word 0x12.
    // Documented in docs/link-protocol.md rather than set here, because
    // setting it and setting it identically at the far end are one decision.
    // false = do NOT use RFO, i.e. use PA_BOOST. See board::kUseRfo -- the
    // flag's sense is the opposite of what the name suggests, and getting it
    // wrong transmits into an unconnected pin with no error anywhere.
    radio.setTxPower(txPowerDbm, board::kUseRfo);

    char f[12];
    nmea::f(f, board::kDefaultFrequencyMhz, 1);
    SerialUSB.print(F("  RFM95W   ok -- "));
    SerialUSB.print(f);
    SerialUSB.print(F(" MHz, SF7/BW125/CR4-5, sync 0x12, "));
    SerialUSB.print(txPowerDbm);
    SerialUSB.println(F(" dBm PA_BOOST"));
    return true;
}

bool begin(RH_RF95& radio) {
    return begin(radio, board::kDefaultTxPowerDbm);
}

}  // namespace rfm95
