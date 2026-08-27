// Timestamps, from the only clock this board actually has.
//
// The Pro RF has no RTC crystal and no backup cell, so at power-on it knows
// nothing but milliseconds since reset. `millis()` is fine for measuring
// intervals and useless for saying when something happened -- and a log that
// will be read back hours later needs the latter.
//
// The SAMD21 does have a hardware RTC peripheral, which RTCZero drives. It
// keeps time across a reset as long as power is held, but it starts at an
// arbitrary value. It is set from the host over serial -- see pollSerial()
// and scripts/set-clock.sh -- and stays right until the board loses power.

#pragma once

#include <stdint.h>

namespace rtc {

void begin();

// Set from a Unix epoch, which is how the host sets it:
//
//     ./scripts/set-clock.sh          sends the desktop's current UTC
//
// The SAMD21's RTC keeps running across a reset as long as the rail stays up,
// and reflashing does not drop the rail -- so this survives an upload and only
// needs doing once per power cycle.
void setFromEpoch(uint32_t unixSeconds, const char* source);

// Watches SerialUSB for a clock-set command and applies it. Call from loop().
// The command is one line:
//
//     T<unix-seconds>\n
//
// Anything else is ignored, so this is safe to call even when the same port
// carries keystrokes meant for something else.
void pollSerial();

// The current time, in the shape lib/qwiic's ClockReader wants. Wire them
// together in the APPLICATION:
//
//     qwiic::setClockReader(&rtc::readClock);
//
// so neither library has to include the other.
void readClock(uint8_t& h, uint8_t& m, uint8_t& s);

// Has the clock ever been set?
bool haveRealTime();

// "hh:mm:ss" once the clock has been set, otherwise "+ssss.s" uptime. Always
// distinguishable, so nothing in a log can be mistaken for wall-clock time
// when it is not.
uint8_t stamp(char* buf, uint8_t cap);

// A longer human-readable description for the health report. Returns false if
// the clock has never been set.
bool describe(char* buf, uint8_t cap);

}  // namespace rtc
