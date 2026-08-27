// Board definition for the SparkFun Pro RF - LoRa, 915MHz (SAMD21).
//
// ONE definition of the pin map. Every other file takes its pins from here.
//
// The radio is an RFM95W (Semtech SX1276) on the SAMD21's default SPI bus.
// Unlike the sibling ESP32 gateway project, the SPI bus itself is not the trap
// here -- the Arduino variant wires SPI to the pins the core already uses, so
// plain `SPI` is correct. What is easy to get wrong is that only two of the
// five radio pins appear in SparkFun's headline example: `RH_RF95 rf95(12, 6)`
// names chip select and DIO0 and says nothing about RST, DIO1 or DIO2.

#pragma once

#include <Arduino.h>

namespace board {

// ---------------------------------------------------------------- radio ----
// Confirmed against SparkFun's hookup guide, which gives both forms:
//   RadioHead: RH_RF95 rf95(12, 6);
//   LMIC:      { .nss = 12, .rst = 7, .dio = {6, 10, 11} }
constexpr uint8_t kPinRadioCs   = 12;  // NSS
constexpr uint8_t kPinRadioDio0 = 6;   // TxDone / RxDone / CadDone -- the IRQ
constexpr uint8_t kPinRadioRst  = 7;   // active low
constexpr uint8_t kPinRadioDio1 = 10;  // RxTimeout / FhssChangeChannel
constexpr uint8_t kPinRadioDio2 = 11;  // FhssChangeChannel

// The RFM95W is the +20 dBm build: PA_BOOST is the only output path, and RFO
// is not wired to anything.
//
// READ THE PARAMETER NAME. RadioHead's signature is
//
//     setTxPower(int8_t power, bool useRFO = false)
//
// The flag selects RFO, NOT PA_BOOST. So the value to pass here is FALSE, and
// `setTxPower(14, true)` -- which SparkFun's hookup guide prints, which the
// 2019 sketches used, and which this project copied -- routes the output to a
// pin that goes nowhere.
//
// The symptom is total, silent, and looks like everything else: the radio
// initialises, reports no error, says "sent", genuinely enters TX mode, and
// the far end hears nothing at all. Diagnosed 2026-08-21 with the `radio_regs`
// tool, which read RegPaConfig back as 0x7E -- PaSelect = 0, i.e. RFO. The
// correct value is 0x8C.
constexpr bool kUseRfo = false;

// ------------------------------------------------------------------ I/O ----
constexpr uint8_t kPinStatusLed = 13;

// --------------------------------------------------------------- link ------
// The 2019 sketches disagreed with each other: BaseStation.ino used 915.0 and
// ReceiverStation.ino / SMD21RFProRF-Client.ino used 921.2, while every one of
// them printed a banner claiming some third number. Two radios on different
// frequencies hear nothing and report no error, which is the same silent
// failure as a wrong pin map. Pick once, here.
//
// Settled by reading it off the board. The firmware that was actually flashed
// in 2019 holds the IEEE-754 float 915.0 at flash offset 0xf588, in the
// initialised-data image -- exactly where `float frequency = 915;` lives. The
// only other frequency in the image is 434.0, and that one sits in a literal
// pool inside RH_RF95::init(), i.e. it is RadioHead's own default, not a
// choice anyone made.
//
// So the last working pair was BaseStation.ino (915.0) and this node (915.0).
// The 921.2 in ReceiverStation.ino and SMD21RFProRF-Client.ino is the older,
// abandoned setting. Matching the hardware beats matching the example code.
//
// 915.0 MHz is the centre of the 902-928 MHz US ISM band. It sits inside the
// US915 LoRaWAN uplink block (902.3-914.9 is channels 0-63; 915.0 is between
// that and the 64-channel 500 kHz group), so a LoRaWAN network in earshot is
// nearby -- but the sync word is 0x12, not LoRaWAN's 0x34, so neither hears
// the other. It is also clear of the ESP32 gateway's 903.9.
constexpr float kDefaultFrequencyMhz = 915.0f;

// RadioHead's Bw125Cr45Sf128 -- BW 125 kHz, CR 4/5, SF7, explicit header, CRC
// on, 8-symbol preamble, sync word 0x12. This is both the RH_RF95 default and
// the SX1276 reset default, which is why the 2019 sketches worked without ever
// configuring the modem. Written down because the moment anything else has to
// receive these packets, all of it has to match.
// Transmit power, dBm. PA_BOOST only on this module, so RadioHead clamps the
// value to 2..20 and there is no way to go lower in software.
//
// 14 dBm is the deployment value. It is far too much for a bench: two of these
// on one desk at 30 cm see about -3 dBm at the receiver, which is roughly
// 17 dB into compression. Override it at build time rather than editing here:
//
//     make run-node TXPOWER=2
//
// See docs/antenna.md for the link budget and for why 2 dBm still is not low
// enough at 30 cm.
#ifndef PRORF_TX_POWER_DBM
#define PRORF_TX_POWER_DBM 14
#endif
constexpr uint8_t kDefaultTxPowerDbm = PRORF_TX_POWER_DBM;
static_assert(kDefaultTxPowerDbm >= 2 && kDefaultTxPowerDbm <= 20,
              "PA_BOOST accepts 2..20 dBm; RadioHead silently clamps anything "
              "outside that, so a value it cannot honour is a build error here "
              "rather than a surprise on the air.");

// ------------------------------------------------------------ Qwiic bus ----
// The Qwiic connector is plain I2C on `Wire`. Addresses of the devices on this
// project's daisy chain, so a probe can name what it finds.
constexpr uint8_t kAddrCcs811    = 0x5B;  // alternate 0x5A
constexpr uint8_t kAddrBme280    = 0x77;  // alternate 0x76
constexpr uint8_t kAddrOpenLog   = 0x2A;

// 400 kHz for the sensors. The OpenLog cannot keep up at that speed and is
// driven at 50 kHz per transaction instead -- see lib/datalog.
constexpr uint32_t kI2cClockHz = 400000UL;

}  // namespace board
