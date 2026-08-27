// The sensor board -- the application this project exists to build.
//
// Finishes what etc/reference/AllSensorsLoRa.ino started, keeping its
// conventions and dropping its worst property.
//
//   read the Qwiic chain in a repeating slot cycle
//     -> one sentence per device, in the reference's $XX form
//     -> prefixed with a sequence number, the reference's %04d: form
//     -> logged to the SD card, the reference's %c: form
//     -> transmitted over LoRa
//
// WHAT CHANGED FROM 2019, and why:
//
//   * NO ACK WAIT. The 2019 sensor board blocked up to a second after every packet
//     waiting for a base station that is now dead -- about 91% of its life,
//     during which no sensor was read at all.

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <RH_RF95.h>

#include <board.h>
#include <datalog.h>
#include <nmea.h>
#include <qwiic.h>
#include <rfm95.h>
#include <telemetry.h>

namespace {

constexpr uint8_t  kNodeId        = 1;
constexpr uint32_t kSlotMs        = 250;    // x kDeviceCount = 1 s cycle
constexpr uint32_t kHealthEveryMs = 30000;
constexpr uint32_t kSyncEveryMs   = 15000;
// FAT 8.3 short names only, and fixed rather than dated: the board has no
// clock at setup(), so there is nothing to name a file after.
constexpr char     kLogFile[]     = "SENSOR.LOG";

RH_RF95 g_radio(board::kPinRadioCs, board::kPinRadioDio0);

bool     g_radioOk   = false;
uint32_t g_seq       = 0;
uint8_t  g_slot      = 0;
uint32_t g_slotStart = 0;
uint32_t g_lastHealth = 0;
uint32_t g_lastSync  = 0;
uint32_t g_cycles    = 0;
uint32_t g_sent      = 0;
uint32_t g_logged    = 0;

// Cumulative mean of the OpenLog write, as the 2019 firmware reported it. Kept
// because it is the number that matters for stage 4: writes were measured at
// 3-5 ms warm but 185-310 ms once the file grew, and the call BLOCKS.
uint32_t g_logCount   = 0;
uint64_t g_logTotalUs = 0;
uint32_t g_logMaxUs   = 0;

char g_sentence[128];
char g_line[192];

// UPTIME, not wall-clock, and that is deliberate rather than a limitation
// worked around. This board has no RTC crystal, no backup cell and nothing to
// learn the time from -- it is a transmitter with no host attached. A stamp
// that LOOKED like a date would be a fiction, and a fiction in a log is worse
// than an honest counter.
//
// The receiver has a host and can be given a real clock, so it tags what it
// hears with a real time. The sequence number is what ties the two together:
// one record, one number, correlatable across both logs regardless of what
// either end calls "now".
void uptimeStamp(char* out, uint8_t cap) {
    const uint32_t ms = millis();
    snprintf(out, cap, "+%lu.%lu",
             static_cast<unsigned long>(ms / 1000),
             static_cast<unsigned long>((ms % 1000) / 100));
}

void emit(const char* record) {
    char stamp[16];
    uptimeStamp(stamp, sizeof(stamp));

    // %04d: -- the reference's sequence prefix. The sequence is per RECORD,
    // not per cycle, so a device that produced nothing leaves a visible gap
    // at the receiver rather than an empty field.
    snprintf(g_line, sizeof(g_line), "%04lu:%s",
             static_cast<unsigned long>(g_seq % 10000), record);

    // 1. Serial, for a human watching.
    SerialUSB.print(stamp);
    SerialUSB.print(' ');
    SerialUSB.println(g_line);

    // 2. The card, BEFORE the air.
    if (datalog::present()) {
        char logLine[208];
        // %c: -- the reference's log prefix. 'I' is the only value the 2019
        // firmware was ever seen to use.
        snprintf(logLine, sizeof(logLine), "%c:%s %s", 'I', stamp, g_line);
        const uint32_t t0 = micros();
        datalog::line(logLine);
        const uint32_t dt = micros() - t0;
        g_logCount++;
        g_logTotalUs += dt;
        if (dt > g_logMaxUs) g_logMaxUs = dt;
        g_logged++;
    }

    // 3. The air. No wait, no ACK.
    if (g_radioOk) {
        telemetry::Telemetry pkt;
        pkt.header.magic   = telemetry::kMagic;
        pkt.header.version = telemetry::kVersion;
        pkt.header.type    = telemetry::kTypeTelemetry;
        pkt.header.nodeId  = kNodeId;
        pkt.header.seq     = g_seq;
        const uint8_t len  = static_cast<uint8_t>(strlen(g_line));
        if (len <= telemetry::kMaxRecordLen) {
            memcpy(pkt.record, g_line, len + 1);
            digitalWrite(board::kPinStatusLed, HIGH);
            g_radio.send(reinterpret_cast<uint8_t*>(&pkt), telemetry::telemetryLen(len));
            // Bounded. A full record is ~256 ms of airtime at SF7/BW125 and
            // the largest possible packet is 395 ms, so 1 s covers the worst
            // case. If it expires the radio is stuck and the next slot finds
            // out rather than this one hanging.
            g_radio.waitPacketSent(1000);
            digitalWrite(board::kPinStatusLed, LOW);
            g_sent++;
        }
    }

    g_seq++;
}

void health() {
    SerialUSB.println();
    SerialUSB.print(F("-- health: "));
    SerialUSB.print(g_cycles);   SerialUSB.print(F(" cycles, "));
    SerialUSB.print(g_seq);      SerialUSB.print(F(" records, "));
    SerialUSB.print(g_sent);     SerialUSB.print(F(" sent, "));
    SerialUSB.print(g_logged);   SerialUSB.println(F(" logged --------"));

    for (uint8_t i = 0; i < qwiic::kDeviceCount; i++) {
        qwiic::formatStats(static_cast<qwiic::DeviceId>(i), g_line, sizeof(g_line));
        SerialUSB.println(g_line);
    }

    if (g_logCount) {
        snprintf(g_line, sizeof(g_line),
                 "  SD write us mean/max %lu/%lu   (BLOCKING -- stage 4 risk)",
                 static_cast<unsigned long>(g_logTotalUs / g_logCount),
                 static_cast<unsigned long>(g_logMaxUs));
        SerialUSB.println(g_line);
    } else {
        SerialUSB.println(F("  SD       not logging"));
    }

    SerialUSB.println(F("--------------------------------------------------"));
}

// Reprinted whenever a terminal attaches, not only at boot.
//
// The board resets itself the instant an upload finishes, so setup() runs and
// prints with no host attached -- and the Arduino SAMD core DISCARDS CDC
// writes when nobody is listening. A monitor started a second later sees
// nothing and the board looks dead. This cost an hour before probe grew the
// same trick.
void banner() {
    SerialUSB.println();
    SerialUSB.println(F("=================================================="));
    SerialUSB.println(F(" Pro RF sensor board"));
    SerialUSB.println(F("=================================================="));
    for (uint8_t i = 0; i < qwiic::kDeviceCount; i++) {
        const qwiic::Device& d = qwiic::device(static_cast<qwiic::DeviceId>(i));
        SerialUSB.print(F("  "));
        SerialUSB.print(d.name);
        SerialUSB.print(F("   0x"));
        SerialUSB.print(d.address, HEX);
        SerialUSB.println(d.present ? F("  present") : F("  ABSENT"));
    }
    SerialUSB.print(F("  OpenLog  "));
    SerialUSB.println(datalog::present() ? F("logging to SENSOR.LOG") : F("ABSENT"));
    SerialUSB.print(F("  radio    "));
    SerialUSB.println(g_radioOk ? F("up") : F("DOWN"));
    SerialUSB.print(F("  cycle    "));
    SerialUSB.print(kSlotMs); SerialUSB.print(F(" ms x "));
    SerialUSB.print(qwiic::kDeviceCount); SerialUSB.print(F(" = "));
    SerialUSB.print(kSlotMs * qwiic::kDeviceCount); SerialUSB.println(F(" ms"));
    SerialUSB.println(F("--------------------------------------------------"));
}



}  // namespace

void setup() {
    pinMode(board::kPinStatusLed, OUTPUT);
    SerialUSB.begin(115200);
    const uint32_t deadline = millis() + 10000;
    while (!SerialUSB && millis() < deadline) delay(10);

    // QUIETEN THE RADIO BEFORE PROBING I2C.
    //
    // An upload resets the SAMD21 and nothing else. The SX1276 keeps whatever
    // mode the previous run left it in -- typically continuous receive, around
    // 12 mA -- while this code brings up a chain of five Qwiic devices on the
    // same 3.3 V regulator.
    //
    // The symptom was the whole chain flapping between all-present and
    // all-absent across identical flashes, while a small i2c_scan sketch found
    // every device every time. The difference between those two programs is
    // the radio. Reset drops the SX1276 to standby and costs 20 ms.
    rfm95::reset();

    qwiic::begin();

    SerialUSB.print(F("  OpenLog  "));
    SerialUSB.println(datalog::begin(kLogFile) ? F("ok -- SENSOR.LOG")
                                               : F("absent -- NOT LOGGING"));

    g_radioOk = rfm95::begin(g_radio);
    if (!g_radioOk) SerialUSB.println(F("           logging only -- nothing will transmit"));

    banner();

    g_slotStart  = millis();
    g_lastHealth = millis();
    g_lastSync   = millis();
}

void loop() {
    // Reprint on the false->true edge of SerialUSB, so attaching a monitor at
    // any time shows what the board actually found.
    static bool wasConnected = false;
    const bool isConnected = static_cast<bool>(SerialUSB);
    if (isConnected && !wasConnected) { delay(200); banner(); }
    wasConnected = isConnected;

    // Anything that starves it loses whole sentence bursts with no error.
    // Cheap, and must run often enough to hit its 30 ms blip: this is called
    // every pass, not once per slot.

    const uint32_t now = millis();
    if ((now - g_slotStart) < kSlotMs) {
        delay(2);
        return;
    }
    g_slotStart = now;

    const qwiic::DeviceId id = static_cast<qwiic::DeviceId>(g_slot);
    if (qwiic::readInto(id, g_sentence, sizeof(g_sentence)) > 0) {
        emit(g_sentence);
    }

    if (++g_slot >= qwiic::kDeviceCount) { g_slot = 0; g_cycles++; }

    // Flush the card periodically. Data written but not synced is lost if
    // power drops, and this node is battery powered and carried around: there
    // is no orderly shutdown to sync at.
    if ((millis() - g_lastSync) >= kSyncEveryMs) {
        g_lastSync = millis();
        datalog::sync();
    }
    if ((millis() - g_lastHealth) >= kHealthEveryMs) {
        g_lastHealth = millis();
        health();
    }
}
