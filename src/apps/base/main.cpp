// The base station.
//
// Listens, prints every record to USB serial as it arrives, LOGS IT TO ITS OWN
// SD CARD, and answers with an ACK carrying the sequence state and the link
// quality it measured.
//
// Nothing here is required for the sensor board to work -- that board logs
// regardless -- so the base is a second, independent record and a live view,
// not a dependency.
//
// WHY LOG AT BOTH ENDS
//
// The two cards are the whole point of the sequence numbers. The sensor board
// writes every record it produces; the base writes every record it receives.
// Diff them and what is missing from the base is exactly what the radio lost.
// Neither card alone can tell you that.
//
// So the RECORD PORTION of each log line is byte-identical between the two.
// The prefixes differ -- 'I' for the sensor's own reading, 'R' for a reception
// -- and both are `<char>:<hh:mm:ss> `, a fixed 12 characters, so either strips
// with one sed:
//
//     sed 's/^.:[0-9:]* //' SENSOR.LOG > a
//     sed 's/^.:[0-9:]* //' BASE.LOG   > b
//     diff a b        # every line only in `a` was lost on the air
//
// The timestamps deliberately do NOT match: the sensor stamps when it read the
// sensor, the base stamps when it heard the packet. Two clocks, two events.

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <RH_RF95.h>

#include <board.h>
#include <datalog.h>
#include <nmea.h>
#include <rfm95.h>
#include <rtc.h>
#include <telemetry.h>

namespace {

constexpr uint8_t  kBaseId         = 0;
constexpr uint32_t kSyncEveryMs    = 15000;
constexpr uint32_t kQuietReportMs  = 10000;
constexpr uint32_t kHealthEveryMs  = 30000;
constexpr char     kLogFile[]      = "BASE.LOG";

RH_RF95 g_radio(board::kPinRadioCs, board::kPinRadioDio0);

bool     g_radioOk    = false;
uint32_t g_received   = 0;   // distinct telemetry packets accepted
uint32_t g_logged     = 0;
uint64_t g_logTotalUs = 0;
uint32_t g_logMaxUs   = 0;
uint32_t g_foreign    = 0;   // valid LoRa, but not ours
uint32_t g_highestSeq = 0;
uint32_t g_firstSeq   = 0;    // the first sequence THIS base actually saw
bool     g_haveAny    = false;
uint32_t g_lastPacket = 0;
uint32_t g_lastSync   = 0;
uint32_t g_lastQuiet  = 0;
uint32_t g_lastHealth = 0;
int16_t  g_lastRssi   = 0;

char g_line[224];

// Serial and the card together, so a line is never in one and not the other.
//
// The output buffer is SEPARATE from g_line on purpose. Callers build their
// text in g_line and pass it here; writing the formatted result back into
// g_line meant snprintf read and wrote the same memory. The visible result was
// "Q:06:55:15 Q:06:55:15 40,8198,..." -- the prefix doubled and the "$PJPR,"
// eaten.
void record(char kind, const char* text) {
    static char out[256];
    char stamp[16];
    rtc::stamp(stamp, sizeof(stamp));
    snprintf(out, sizeof(out), "%c:%s %s", kind, stamp, text);
    SerialUSB.println(out);
    if (datalog::present()) {
        // datalog::line() returns void deliberately. A batching variant that
        // returned success was measured and rejected: it makes the blocking
        // window an order of magnitude worse for no throughput gain. Timing is
        // measured here instead of inferred from a return value.
        const uint32_t t0 = micros();
        datalog::line(out);
        const uint32_t dt = micros() - t0;
        g_logged++;
        g_logTotalUs += dt;
        if (dt > g_logMaxUs) g_logMaxUs = dt;
    }
}

bool startRadio() {
    // Quieten the radio before anything else touches I2C. An upload resets the
    // SAMD21 and nothing else, so the SX1276 arrives in whatever mode the last
    // run left it -- typically continuous receive at ~12 mA on the same 3.3 V
    // rail the Qwiic chain uses. See docs/qwiic-timing.md.
    rfm95::reset();
    return rfm95::begin(g_radio);
}

void sendAck(uint32_t seq, int16_t rssi, int8_t snr) {
    telemetry::Ack ack;
    ack.header.magic   = telemetry::kMagic;
    ack.header.version = telemetry::kVersion;
    ack.header.type    = telemetry::kTypeAck;
    ack.header.nodeId  = kBaseId;
    ack.header.seq     = seq;
    ack.highest        = g_highestSeq;
    ack.received       = g_received;
    ack.rssi           = rssi;
    ack.snr            = snr;
    ack.reserved       = 0;

    g_radio.send(reinterpret_cast<uint8_t*>(&ack), sizeof(ack));
    g_radio.waitPacketSent(500);
}

void handlePacket(uint8_t* buf, uint8_t len) {
    const int16_t rssi = g_radio.lastRssi();
    const int8_t  snr  = static_cast<int8_t>(g_radio.lastSNR());
    g_lastRssi = rssi;

    if (!telemetry::headerLooksValid(buf, len)) {
        // Something is on frequency with the same modem settings and is not
        // ours. Worth recording: on a shared ISM band it is the difference
        // between "the sensor board is dead" and "that is someone else".
        g_foreign++;
        snprintf(g_line, sizeof(g_line), "$PJPX,foreign,%u,%d,%d",
                 len, static_cast<int>(rssi), static_cast<int>(snr));
        nmea::appendChecksum(g_line, sizeof(g_line));
        record('X', g_line);
        return;
    }

    const telemetry::Header* h = reinterpret_cast<const telemetry::Header*>(buf);
    if (h->type != telemetry::kTypeTelemetry) return;

    const telemetry::Telemetry* t = reinterpret_cast<const telemetry::Telemetry*>(buf);

    // NUL-terminate defensively. Everything past here treats the record as a C
    // string, and the sender's length is not something to trust.
    buf[len - 1] = '\0';

    g_received++;
    const uint32_t seq = h->seq;
    if (!g_haveAny) {
        // Loss is counted from where THIS base started listening. Counting
        // from zero reported every packet sent before it booted as lost --
        // 8158 phantom losses on the first real run.
        g_firstSeq   = seq;
        g_highestSeq = seq;
        g_haveAny    = true;
    } else if (seq > g_highestSeq) {
        g_highestSeq = seq;
    }
    g_lastPacket = millis();

    digitalWrite(board::kPinStatusLed, HIGH);

    // 1. The record verbatim -- byte-identical to what the sensor board wrote
    //    to its own card, which is what makes the two logs diffable.
    record('R', t->record);

    // 2. The base's own view of it, as a separate line, so it can be filtered
    //    out before diffing.
    // Span actually observed, not span since the sensor board booted.
    const uint32_t expected = g_highestSeq - g_firstSeq + 1;
    const uint32_t missing  = (expected > g_received) ? expected - g_received : 0;
    // "<seq>:$PJPR,..." -- the SEQUENCE PREFIX matches the received records,
    // which arrive as "<seq>:$XXWTH,...". Without it the two line shapes
    // differed and nothing could strip a uniform prefix off both:
    //
    //   R:+7405.9 4401:$XXWTH,...      <- seq present
    //   Q:+7405.9 $PJPR,4401,...       <- seq absent, and in a different place
    //
    // The sequence still appears as the first FIELD too, and that duplication
    // is deliberate: a sentence has to stay decodable on its own once it is
    // separated from the line that carried it.
    // BUILD AND CHECKSUM THE SENTENCE FIRST, THEN PREFIX THE SEQUENCE.
    // The order matters and getting it wrong is silent: nmeaChecksum() skips a
    // '$' only when it is the FIRST character, so checksumming
    // "4401:$PJPR,..." would XOR the prefix in too and emit a checksum no
    // parser accepts. Verified against a real record -- "$XXWTH,...*41" is 0x41
    // over the sentence alone and 0x5E with the prefix included, so the
    // sequence prefix is OUTSIDE the checksum on received records and must be
    // outside it here.
    char sentence[96];
    snprintf(sentence, sizeof(sentence), "$PJPR,%lu,%lu,%lu,%lu,%d,%d",
             static_cast<unsigned long>(seq),
             static_cast<unsigned long>(g_received),
             static_cast<unsigned long>(expected),
             static_cast<unsigned long>(missing),
             static_cast<int>(rssi), static_cast<int>(snr));
    nmea::appendChecksum(sentence, sizeof(sentence));
    snprintf(g_line, sizeof(g_line), "%lu:%s",
             static_cast<unsigned long>(seq), sentence);
    record('Q', g_line);

    sendAck(seq, rssi, snr);
    digitalWrite(board::kPinStatusLed, LOW);
}

// A field legend for whoever is watching the serial port, printed at startup
// and then periodically -- a log that scrolls for an hour is useless to
// someone who joins halfway through and sees "R:+7405.9 4401:$XXWTH,1989.6".
//
// TWO RULES, both of which matter more than the text:
//
//   1. EVERY LINE STARTS WITH '#'. That is the comment convention, so an
//      automated reader strips the whole legend with `grep -v '^#'` and never
//      has to know what any individual line says.
//   2. SERIAL ONLY -- this must NEVER go through record(). record() writes to
//      the SD card as well, and BASE.LOG has to stay byte-comparable with
//      SENSOR.LOG so the two diff directly and every difference is a lost
//      packet. Legend lines on the card would show up as spurious differences
//      on every single diff.
void legend() {
    SerialUSB.println(F("#"));
    SerialUSB.println(F("# ---- legend (lines starting with # are not data) ----"));
    SerialUSB.println(F("# line:  <kind>:<time> [<seq>:]<sentence>*<checksum>"));
    SerialUSB.println(F("#   kind  R received record   Q link quality"));
    SerialUSB.println(F("#         X foreign traffic   H silence heartbeat"));
    SerialUSB.println(F("#   time  hh:mm:ss UTC once the clock is set,"));
    SerialUSB.println(F("#         otherwise +seconds since this board booted"));
    SerialUSB.println(F("#   seq   transmitter's sequence number; gaps are lost packets"));
    SerialUSB.println(F("#   checksum covers the sentence ONLY, not the seq prefix"));
    SerialUSB.println(F("#"));
    SerialUSB.println(F("# $XXWTH,<altM>,<tempC>,<hPa>,<humidity%>"));
    SerialUSB.println(F("# $XXCO2,<resistance>,<eCO2ppm>,<tvocPpb>"));
    SerialUSB.println(F("#        EMPTY field = not measured; 0 = measured zero."));
    SerialUSB.println(F("# $PJPR,<seq>,<received>,<expected>,<missing>,<rssi>,<snr>"));
    SerialUSB.println(F("#        counted from when THIS board started listening,"));
    SerialUSB.println(F("#        not from the sensor board's packet 0"));
    SerialUSB.println(F("# $PJPX,foreign,<len>,<rssi>,<snr>   not ours, same frequency"));
    SerialUSB.println(F("# $PJPH,<seconds quiet>,<received>,<missing>   heartbeat"));
    SerialUSB.println(F("# ------------------------------------------------------"));
}

// Wait for a real clock before listening, because THIS board is the one that
// can have one.
//
// The transmitter has no RTC crystal, no backup cell and no host, so it stamps
// its records with uptime and says so. The base station is different: it is
// attached to a machine that knows the time, which makes it the right end to
// put a real timestamp on anything. Every received record is tagged with the
// clock read HERE, at the moment of reception.
//
// TWO MINUTES, and the length is chosen from how the board is actually used.
// It is unplugged at the test site, carried to where it will live, and plugged
// in -- and the clock gets set by whatever is monitoring it, seconds later.
// Two minutes covers a person finding the right terminal window.
//
// Waiting costs very little here. WITHOUT AN OPENLOG AND WITHOUT A MONITOR
// ATTACHED, a received record has nowhere to go anyway: it is echoed to a
// serial port nobody is reading and then discarded. So the wait is only ever
// spent in the state where the output would have been thrown away regardless.
//
// It is still bounded rather than infinite. A base that refuses to start
// because nobody set its clock would sit silent forever, and "it looks dead"
// is a much worse failure than "its stamps are relative".
//
// Any keypress skips the wait, which is what you want when the board is
// plugged into a bench supply with no host script running.
void waitForClock() {
    constexpr uint32_t kWaitMs = 120000;
    if (rtc::haveRealTime()) return;

    SerialUSB.println();
    SerialUSB.println(F("-- waiting for the clock ---------------------------"));
    SerialUSB.println(F("  Received records are tagged with THIS board's clock,"));
    SerialUSB.println(F("  so it is worth setting before anything is logged."));
    SerialUSB.println(F("      make clock          (from another terminal)"));
    SerialUSB.println(F("  Any key skips. Giving up after 2 min and using uptime."));

    // DRAIN FIRST. Opening the port toggles DTR and can leave a stray byte in
    // the buffer, so a bare available() check is true before anyone has
    // pressed anything -- which skipped this wait every single time and made
    // the feature look like it was not there at all.
    while (SerialUSB.available()) SerialUSB.read();

    const uint32_t deadline = millis() + kWaitMs;
    uint32_t lastDot = 0;
    while (millis() < deadline) {
        rtc::pollSerial();

        // KEEP LISTENING WHILE WAITING. A base station that stops receiving
        // for thirty seconds to ask about its clock has thrown away about
        // ninety records to improve the timestamps on the rest, which is a bad
        // trade. Records that arrive during the wait are stamped with uptime,
        // exactly as they would have been had the clock never been set.
        if (g_radioOk && g_radio.available()) {
            uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];
            uint8_t len = sizeof(buf);
            if (g_radio.recv(buf, &len) && len > 0) handlePacket(buf, len);
        }
        if (rtc::haveRealTime()) {
            SerialUSB.println();
            SerialUSB.print(F("  clock   "));
            if (rtc::describe(g_line, sizeof(g_line))) SerialUSB.println(g_line);
            return;
        }
        // A keypress means "get on with it". Anything the host sends that is
        // NOT a clock line lands here, which is the behaviour you want: a
        // human hitting return should not have to wait out the timeout.
        if (SerialUSB.available()) { while (SerialUSB.available()) SerialUSB.read(); break; }
        if (millis() - lastDot >= 1000) { lastDot = millis(); SerialUSB.print('.'); }
        delay(10);
    }
    SerialUSB.println();
    SerialUSB.println(F("  clock   NOT SET -- stamps are uptime, not UTC"));
}

void banner() {
    SerialUSB.println();
    SerialUSB.println(F("=================================================="));
    SerialUSB.println(F(" Pro RF base station"));
    SerialUSB.println(F("=================================================="));
    SerialUSB.print(F("  OpenLog  "));
    SerialUSB.println(datalog::present() ? F("logging to BASE.LOG") : F("ABSENT -- receiving only"));
    SerialUSB.print(F("  radio    "));
    SerialUSB.println(g_radioOk ? F("listening") : F("DOWN -- nothing will be heard"));
    SerialUSB.println(F("  log lines: R = a received record, Q = link quality,"));
    SerialUSB.println(F("             X = foreign traffic on our frequency"));
    // Only advertise the clock when there isn't one. A banner that tells you
    // to do something already done is noise, and noise in a banner is how a
    // real warning gets skipped over.
    SerialUSB.print(F("  clock    "));
    if (rtc::describe(g_line, sizeof(g_line))) SerialUSB.println(g_line);
    else SerialUSB.println(F("NOT SET -- stamps are uptime; BOARD=base make clock"));
    SerialUSB.println(F("--------------------------------------------------"));
}

void health() {
    const uint32_t expected = g_haveAny ? (g_highestSeq - g_firstSeq + 1) : 0;
    const uint32_t missing  = (expected > g_received) ? expected - g_received : 0;
    // Loss in tenths of a percent, integer -- newlib-nano's printf has no
    // floating point and a %f emits nothing at all.
    const uint32_t lossTenths = expected ? (missing * 1000UL / expected) : 0;

    SerialUSB.println();
    snprintf(g_line, sizeof(g_line),
             "-- base: %lu received, %lu missing of %lu (%lu.%01lu%% loss), "
             "%lu foreign, rssi %d --",
             static_cast<unsigned long>(g_received),
             static_cast<unsigned long>(missing),
             static_cast<unsigned long>(expected),
             static_cast<unsigned long>(lossTenths / 10),
             static_cast<unsigned long>(lossTenths % 10),
             static_cast<unsigned long>(g_foreign),
             static_cast<int>(g_lastRssi));
    SerialUSB.println(g_line);

    if (datalog::present()) {
        snprintf(g_line, sizeof(g_line),
                 "  SD       %lu lines logged, us mean/max %lu/%lu (BLOCKING)",
                 static_cast<unsigned long>(g_logged),
                 static_cast<unsigned long>(g_logged ? g_logTotalUs / g_logged : 0),
                 static_cast<unsigned long>(g_logMaxUs));
        SerialUSB.println(g_line);
    } else {
        SerialUSB.println(F("  SD       not logging"));
    }
    SerialUSB.print(F("  clock   "));
    if (rtc::describe(g_line, sizeof(g_line))) SerialUSB.println(g_line);
    else SerialUSB.println(F("NOT SET -- BOARD=base make clock"));
    SerialUSB.println(F("--------------------------------------------------"));
}

}  // namespace

void setup() {
    pinMode(board::kPinStatusLed, OUTPUT);
    SerialUSB.begin(115200);
    const uint32_t deadline = millis() + 10000;
    while (!SerialUSB && millis() < deadline) delay(10);

    g_radioOk = startRadio();

    Wire.begin();
    Wire.setClock(board::kI2cClockHz);
    // Same settle-and-retry the sensor board needs: the first transactions
    // after Wire.begin() fail while later ones succeed, and a one-shot probe
    // therefore drops a device that is working.
    delay(50);
    SerialUSB.print(F("  OpenLog  "));
    SerialUSB.println(datalog::begin(kLogFile) ? F("ok -- BASE.LOG")
                                               : F("absent -- receiving only"));

    rtc::begin();
    waitForClock();
    banner();
    legend();

    g_lastPacket = millis();
    g_lastSync   = millis();
    g_lastQuiet  = millis();
    g_lastHealth = millis();
}

void loop() {
    // Reprint on attach. The board resets itself when an upload finishes and
    // the Arduino SAMD core discards CDC writes with no host attached, so a
    // monitor started a second later would otherwise see an empty screen.
    static bool wasConnected = false;
    const bool isConnected = static_cast<bool>(SerialUSB);
    if (isConnected && !wasConnected) { delay(200); banner(); legend(); }
    wasConnected = isConnected;

    rtc::pollSerial();

    if (g_radioOk && g_radio.available()) {
        uint8_t buf[RH_RF95_MAX_MESSAGE_LEN];
        uint8_t len = sizeof(buf);
        if (g_radio.recv(buf, &len) && len > 0) {
            handlePacket(buf, len);
        }
    }

    const uint32_t now = millis();

    // A silent base and a broken base look identical. Say which.
    if ((now - g_lastQuiet) >= kQuietReportMs) {
        g_lastQuiet = now;
        if ((now - g_lastPacket) >= kQuietReportMs) {
            snprintf(g_line, sizeof(g_line), "$PJPH,%lu,%lu,%lu",
                     static_cast<unsigned long>((now - g_lastPacket) / 1000),
                     static_cast<unsigned long>(g_received),
                     static_cast<unsigned long>(g_haveAny ? g_highestSeq : 0));
            nmea::appendChecksum(g_line, sizeof(g_line));
            record('H', g_line);
        }
    }

    if ((now - g_lastSync) >= kSyncEveryMs) {
        g_lastSync = now;
        datalog::sync();
    }
    if ((now - g_lastHealth) >= kHealthEveryMs) {
        g_lastHealth = now;
        health();
        // Re-printed with each health report: someone who joined the serial
        // session halfway through has never seen it.
        legend();
    }
}
