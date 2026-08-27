#include "rtc.h"

#include <Arduino.h>
#include <RTCZero.h>
#include <stdlib.h>


namespace rtc {
namespace {

RTCZero     g_rtc;
bool        g_set     = false;
const char* g_source  = "unset";
uint32_t g_lastSyncMs = 0;

// Re-discipline every 10 minutes once set. The SAMD21's RTC runs from the
// internal 32 kHz oscillator, which is good for a few seconds a day -- fine
// between syncs, worth correcting when a fix is free.
constexpr uint32_t kResyncMs = 600000;

}  // namespace

void readClock(uint8_t& h, uint8_t& m, uint8_t& s) {
    h = g_rtc.getHours(); m = g_rtc.getMinutes(); s = g_rtc.getSeconds();
}

void begin() {
    g_rtc.begin();
}

void setFromEpoch(uint32_t unixSeconds, const char* source) {
    // Days-since-epoch to civil date. Howard Hinnant's algorithm: exact, no
    // loops, no table, and no 64-bit division on a Cortex-M0+.
    uint32_t days = unixSeconds / 86400UL;
    const uint32_t secondsOfDay = unixSeconds % 86400UL;

    days += 719468UL;
    const uint32_t era = days / 146097UL;
    const uint32_t doe = days - era * 146097UL;
    const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    uint32_t y = yoe + era * 400UL;
    const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const uint32_t mp = (5 * doy + 2) / 153;
    const uint32_t d = doy - (153 * mp + 2) / 5 + 1;
    const uint32_t m = mp + (mp < 10 ? 3 : -9);
    if (m <= 2) y++;

    g_rtc.setTime(static_cast<uint8_t>(secondsOfDay / 3600),
                  static_cast<uint8_t>((secondsOfDay % 3600) / 60),
                  static_cast<uint8_t>(secondsOfDay % 60));
    g_rtc.setDate(static_cast<uint8_t>(d), static_cast<uint8_t>(m),
                  static_cast<uint8_t>(y % 100));

    g_set = true;
    g_source = source;
    g_lastSyncMs = millis();

    char buf[80];
    snprintf(buf, sizeof(buf), "  clock   set from %s: %04lu-%02lu-%02lu %02lu:%02lu:%02lu UTC",
             source,
             static_cast<unsigned long>(y), static_cast<unsigned long>(m),
             static_cast<unsigned long>(d),
             static_cast<unsigned long>(secondsOfDay / 3600),
             static_cast<unsigned long>((secondsOfDay % 3600) / 60),
             static_cast<unsigned long>(secondsOfDay % 60));
    SerialUSB.println(buf);
}

void pollSerial() {
    static char line[24];
    static uint8_t at = 0;

    while (SerialUSB.available()) {
        const char c = static_cast<char>(SerialUSB.read());
        if (c == '\r') continue;
        if (c == '\n') {
            line[at] = '\0';
            if (at > 1 && (line[0] == 'T' || line[0] == 't')) {
                const uint32_t epoch = strtoul(line + 1, nullptr, 10);
                // Sanity: 2024-01-01 .. 2100-01-01. A malformed line must not
                // set the clock to 1970 and make every log after it a lie.
                if (epoch > 1704067200UL && epoch < 4102444800UL) {
                    setFromEpoch(epoch, "host");
                } else {
                    SerialUSB.println(F("  clock   rejected: implausible epoch"));
                }
            }
            at = 0;
        } else if (at < sizeof(line) - 1) {
            line[at++] = c;
        }
    }
}


bool haveRealTime() { return g_set; }

uint8_t stamp(char* buf, uint8_t cap) {
    if (g_set) {
        snprintf(buf, cap, "%02u:%02u:%02u",
                 g_rtc.getHours(), g_rtc.getMinutes(), g_rtc.getSeconds());
    } else {
        // Leading '+' marks it as relative, so no reader can mistake an
        // uptime for a wall clock.
        const uint32_t ms = millis();
        snprintf(buf, cap, "+%lu.%01lu",
                 static_cast<unsigned long>(ms / 1000),
                 static_cast<unsigned long>((ms % 1000) / 100));
    }
    return static_cast<uint8_t>(strlen(buf));
}

bool describe(char* buf, uint8_t cap) {
    if (!g_set) return false;
    // Report the ACTUAL source, not an assumed one. This once printed a fixed
    // source string regardless of where the time actually came from, which was
    // wrong every time the host set it.
    snprintf(buf, cap, "20%02u-%02u-%02u %02u:%02u:%02u UTC (from %s)",
             g_rtc.getYear(), g_rtc.getMonth(), g_rtc.getDay(),
             g_rtc.getHours(), g_rtc.getMinutes(), g_rtc.getSeconds(), g_source);
    return true;
}

}  // namespace rtc
