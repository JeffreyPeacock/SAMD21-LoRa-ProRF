// radio_tx -- beacon numbered packets. This one transmits.
//
// Power defaults to 5 dBm, not the 14 dBm the node uses. This is a bench tool,
// and two radios a metre apart at 14 dBm can saturate the receiver's front
// end; the symptom -- CRC failures when close, success when further away --
// reads as a bug rather than as overload. Raise it for a range test.

#include <Arduino.h>
#include <SPI.h>
#include <RH_RF95.h>

#include <board.h>
#include <nmea.h>
#include <rfm95.h>
#include <telemetry.h>

namespace {

// Bench default, deliberately below board::kDefaultTxPowerDbm.
//
// This cannot be written as `#ifndef PRORF_TX_POWER_DBM / #define 5`: board.h
// is included above and defines that macro to 14 itself, so the guard is
// already satisfied and the 5 never takes effect. The first build of this tool
// announced "14 dBm PA_BOOST" for exactly that reason.
//
// PRORF_TX_POWER_SET is defined only when TXPOWER=n was passed on the command
// line, so an explicit request still wins and the default stays low.
#ifdef PRORF_TX_POWER_SET
constexpr uint8_t  kTxPowerDbm   = PRORF_TX_POWER_DBM;
#else
constexpr uint8_t  kTxPowerDbm   = 5;
#endif
static_assert(kTxPowerDbm >= 2 && kTxPowerDbm <= 20, "PA_BOOST accepts 2..20 dBm");
constexpr uint32_t kIntervalMs   = 2000;
constexpr uint8_t  kNodeId       = 99;   // not the real node

RH_RF95 g_radio(board::kPinRadioCs, board::kPinRadioDio0);
bool     g_ok  = false;
uint32_t g_seq = 0;
uint32_t g_last = 0;

}  // namespace

void setup() {
    pinMode(board::kPinStatusLed, OUTPUT);
    SerialUSB.begin(115200);
    const uint32_t deadline = millis() + 10000;
    while (!SerialUSB && millis() < deadline) delay(10);

    SerialUSB.println();
    SerialUSB.println(F("== radio_tx -- BEACON ============================="));
    g_ok = rfm95::begin(g_radio, kTxPowerDbm);
    g_last = millis();
}

void loop() {
    if (!g_ok) { delay(1000); return; }
    if ((millis() - g_last) < kIntervalMs) return;
    g_last = millis();

    char record[64];
    char n[16];
    strcpy(record, "$PJPB,radio_tx,");
    nmea::f(n, static_cast<float>(g_seq), 0);
    strcat(record, n);
    strcat(record, ",");
    nmea::f(n, static_cast<float>(millis()), 0);
    strcat(record, n);
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
}
