// radio_sweep -- step the transmit power and label every packet with it.
//
// Built to settle a specific disagreement: the receiver reports RSSI around
// -92 dBm for two boards roughly a foot apart at 2 dBm. Free space at 30 cm
// predicts about -15 dBm, so the observed figure is ~77 dB low -- it implies
// two kilometres of separation.
//
// Two explanations, and a sweep tells them apart without anyone having to
// trust the other end's arithmetic:
//
//   * If the far end's RSSI moves one-for-one with the number in the packet,
//     its receive chain is linear and self-consistent, and the argument is
//     only about the absolute offset (which register, which -157/-164 constant).
//   * If it moves less than one-for-one, or not at all, the receiver is
//     compressing, or the reported figure is not really RSSI.
//
// Either way the far end gets a log that labels itself, so no clock has to be
// synchronised and nobody has to remember which reflash was which.
//
// Power is stepped 2 -> 17 dBm. It stops at 17 deliberately: above that
// RadioHead enables PaDac for the +20 dBm mode, which is a different output
// stage with a 1% duty-cycle limit, and mixing it into a linearity test would
// muddy exactly the measurement being made.

#include <Arduino.h>
#include <SPI.h>
#include <RH_RF95.h>

#include <board.h>
#include <nmea.h>
#include <rfm95.h>
#include <telemetry.h>

namespace {

constexpr uint8_t  kSteps[]        = {2, 5, 8, 11, 14, 17};
constexpr uint8_t  kNumSteps       = sizeof(kSteps) / sizeof(kSteps[0]);
constexpr uint8_t  kPacketsPerStep = 5;
constexpr uint32_t kIntervalMs     = 2000;
constexpr uint8_t  kNodeId         = 99;

RH_RF95 g_radio(board::kPinRadioCs, board::kPinRadioDio0);
bool     g_ok    = false;
uint32_t g_seq   = 0;
uint8_t  g_step  = 0;
uint8_t  g_count = 0;
uint32_t g_last  = 0;

// Read RegPaConfig back rather than trusting setTxPower(). This is the whole
// reason the sweep is trustworthy: an earlier bug had the output routed to
// RFO, which is not connected on an RFM95W, and nothing anywhere reported it.
uint8_t readPaConfig() {
    const SPISettings spi(1000000, MSBFIRST, SPI_MODE0);
    SPI.beginTransaction(spi);
    digitalWrite(board::kPinRadioCs, LOW);
    SPI.transfer(0x09 & 0x7F);
    const uint8_t v = SPI.transfer(0x00);
    digitalWrite(board::kPinRadioCs, HIGH);
    SPI.endTransaction();
    return v;
}

void applyPower(uint8_t dbm) {
    g_radio.setTxPower(dbm, board::kUseRfo);

    const uint8_t pa = readPaConfig();
    SerialUSB.println();
    SerialUSB.print(F("== step to "));
    SerialUSB.print(dbm);
    SerialUSB.print(F(" dBm -- RegPaConfig = 0x"));
    if (pa < 0x10) SerialUSB.print('0');
    SerialUSB.print(pa, HEX);
    if (pa & 0x80) {
        SerialUSB.print(F("  PA_BOOST, OutputPower "));
        SerialUSB.print(pa & 0x0F);
        SerialUSB.print(F(" -> "));
        SerialUSB.print((pa & 0x0F) + 2);
        SerialUSB.println(F(" dBm"));
    } else {
        SerialUSB.println(F("  *** RFO -- NOT WIRED, nothing will radiate ***"));
    }
}

}  // namespace

void setup() {
    pinMode(board::kPinStatusLed, OUTPUT);
    SerialUSB.begin(115200);
    const uint32_t deadline = millis() + 10000;
    while (!SerialUSB && millis() < deadline) delay(10);

    SerialUSB.println();
    SerialUSB.println(F("== radio_sweep -- stepping transmit power ========="));
    g_ok = rfm95::begin(g_radio);
    if (g_ok) applyPower(kSteps[0]);
    g_last = millis();
}

void loop() {
    if (!g_ok) { delay(1000); return; }
    if ((millis() - g_last) < kIntervalMs) return;
    g_last = millis();

    const uint8_t dbm = kSteps[g_step];

    // The power is IN the sentence, so the far end's log labels itself.
    char record[64];
    char n[16];
    strcpy(record, "$PJPS,");
    nmea::f(n, static_cast<float>(g_seq), 0);  strcat(record, n);  strcat(record, ",");
    nmea::f(n, static_cast<float>(dbm), 0);    strcat(record, n);  strcat(record, ",");
    nmea::f(n, static_cast<float>(millis()), 0); strcat(record, n);
    nmea::appendChecksum(record, sizeof(record));

    telemetry::Telemetry pkt;
    pkt.header.magic   = telemetry::kMagic;
    pkt.header.version = telemetry::kVersion;
    pkt.header.type    = telemetry::kTypeTelemetry;
    pkt.header.nodeId  = kNodeId;
    pkt.header.seq     = g_seq;
    const uint8_t len  = static_cast<uint8_t>(strlen(record));
    memcpy(pkt.record, record, len + 1);

    digitalWrite(board::kPinStatusLed, HIGH);
    g_radio.send(reinterpret_cast<uint8_t*>(&pkt), telemetry::telemetryLen(len));
    g_radio.waitPacketSent(1000);
    digitalWrite(board::kPinStatusLed, LOW);

    SerialUSB.print(F("sent "));
    SerialUSB.println(record);

    g_seq++;
    if (++g_count >= kPacketsPerStep) {
        g_count = 0;
        g_step = static_cast<uint8_t>((g_step + 1) % kNumSteps);
        applyPower(kSteps[g_step]);
    }
}
