// radio_rx -- listen, and say exactly what arrived.
//
// Receives only, so it is safe with no antenna attached.
//
// Prints every packet with RSSI and SNR. If the payload is one of this
// project's frames it decodes it; otherwise it dumps hex, because "something
// else is on this frequency" and "our node is dead" are different problems
// with identical symptoms on a shared ISM band.

#include <Arduino.h>
#include <SPI.h>
#include <RH_RF95.h>

#include <board.h>
#include <nmea.h>
#include <rfm95.h>
#include <telemetry.h>

namespace {

RH_RF95 g_radio(board::kPinRadioCs, board::kPinRadioDio0);
bool     g_ok = false;
uint32_t g_count = 0;
uint32_t g_lastMs = 0;

void dumpHex(const uint8_t* buf, uint8_t len) {
    for (uint8_t i = 0; i < len; i++) {
        if (buf[i] < 0x10) SerialUSB.print('0');
        SerialUSB.print(buf[i], HEX);
        SerialUSB.print(i % 16 == 15 ? '\n' : ' ');
    }
    if (len % 16) SerialUSB.println();
}

void report(uint8_t* buf, uint8_t len) {
    g_count++;
    g_lastMs = millis();

    SerialUSB.print(F("["));
    SerialUSB.print(g_count);
    SerialUSB.print(F("] len="));
    SerialUSB.print(len);
    SerialUSB.print(F(" rssi="));
    SerialUSB.print(g_radio.lastRssi());
    SerialUSB.print(F("dBm snr="));
    SerialUSB.print(g_radio.lastSNR());
    SerialUSB.println(F("dB"));

    // Too much signal looks like a bug, not like too much signal. Above about
    // -20 dBm the SX1276's front end compresses: RSSI stops tracking, SNR
    // sags, and packets start failing CRC *because the radios are close
    // together*. Moving them further apart then "fixes" it, which sends people
    // hunting for a fault that does not exist.
    const int16_t rssi = g_radio.lastRssi();
    if (rssi > -20) {
        SerialUSB.println(F("      *** RECEIVER OVERLOADED -- too much signal."));
        SerialUSB.println(F("      Drop the transmitter: make run-node TXPOWER=2"));
        SerialUSB.println(F("      2 dBm is the PA_BOOST floor; below that you"));
        SerialUSB.println(F("      need an inline attenuator. docs/antenna.md"));
    }

    if (!telemetry::headerLooksValid(buf, len)) {
        // RadioHead has already stripped its own 4-byte to/from/id/flags
        // header by the time recv() returns, so what is left really is a
        // foreign payload rather than an offset problem.
        SerialUSB.println(F("      not one of ours:"));
        dumpHex(buf, len);
        return;
    }

    const telemetry::Header* h = reinterpret_cast<const telemetry::Header*>(buf);
    SerialUSB.print(F("      type="));
    SerialUSB.print(h->type);
    SerialUSB.print(F(" node="));
    SerialUSB.print(h->nodeId);
    SerialUSB.print(F(" seq="));
    SerialUSB.println(h->seq);

    if (h->type == telemetry::kTypeTelemetry && len > sizeof(telemetry::Header)) {
        buf[len - 1] = '\0';
        const char* record =
            reinterpret_cast<const char*>(buf + sizeof(telemetry::Header));
        SerialUSB.print(F("      "));
        SerialUSB.println(record);
        SerialUSB.print(F("      checksum: "));
        SerialUSB.println(nmea::verifyChecksum(record) ? F("ok") : F("BAD"));
    }
}

}  // namespace

void setup() {
    pinMode(board::kPinStatusLed, OUTPUT);
    SerialUSB.begin(115200);
    const uint32_t deadline = millis() + 10000;
    while (!SerialUSB && millis() < deadline) delay(10);

    SerialUSB.println();
    SerialUSB.println(F("== radio_rx -- listening =========================="));
    g_ok = rfm95::begin(g_radio);
    if (!g_ok) SerialUSB.println(F("           nothing will be heard"));
    g_lastMs = millis();
}

void loop() {
    if (g_ok && g_radio.available()) {
        uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];
        uint8_t len = sizeof(buf);
        if (g_radio.recv(buf, &len) && len > 0) {
            digitalWrite(board::kPinStatusLed, HIGH);
            report(buf, len);
            digitalWrite(board::kPinStatusLed, LOW);
        }
    }

    // A silent receiver and a broken receiver look identical. Say which.
    if ((millis() - g_lastMs) >= 10000) {
        g_lastMs = millis();
        SerialUSB.print(F("... nothing heard for 10 s ("));
        SerialUSB.print(g_count);
        SerialUSB.println(F(" packets total)"));
    }
}
