// NMEA sentence helpers: fixed-point float formatting and checksums.
//
// Why not dtostrf(): the 2019 sketch pulled in <avr/dtostrf.h>, which exists on
// the SAMD21 only because SparkFun's core ships a shim for AVR compatibility.
// Why not sprintf("%f"): the Arduino SAMD core links newlib-nano, whose printf
// has no floating-point support -- `%f` silently emits nothing useful. That is
// the actual reason the original code reached for dtostrf in the first place.
//
// So: format from integers. No libc float path, no core-specific header, and
// the output is identical on any target.

#pragma once

#include <stdint.h>
#include <stdlib.h>   // strtol, used by verifyChecksum
#include <string.h>
#include <math.h>

namespace nmea {

// Writes `value` with `decimals` places into `out` (which must hold at least
// 16 bytes) and returns the number of characters written, excluding the NUL.
// Non-finite values become "nan" -- never a partial number, which would
// corrupt a comma-separated record downstream.
inline uint8_t f(char* out, float value, uint8_t decimals = 2) {
    if (isnan(value) || isinf(value)) {
        strcpy(out, "nan");
        return 3;
    }

    uint32_t scale = 1;
    for (uint8_t i = 0; i < decimals; i++) scale *= 10;

    const bool negative = value < 0.0f;
    if (negative) value = -value;

    // Round half away from zero at the last kept digit.
    const uint32_t scaled = static_cast<uint32_t>(value * scale + 0.5f);
    const uint32_t whole  = scaled / scale;
    const uint32_t frac   = scaled % scale;

    char* p = out;
    if (negative) *p++ = '-';

    char digits[12];
    uint8_t n = 0;
    uint32_t w = whole;
    do { digits[n++] = static_cast<char>('0' + (w % 10)); w /= 10; } while (w);
    while (n) *p++ = digits[--n];

    if (decimals) {
        *p++ = '.';
        uint32_t divisor = scale / 10;
        while (divisor) {
            *p++ = static_cast<char>('0' + ((frac / divisor) % 10));
            divisor /= 10;
        }
    }
    *p = '\0';
    return static_cast<uint8_t>(p - out);
}

// NMEA 0183 checksum: XOR of everything between '$' and '*', exclusive.
inline uint8_t nmeaChecksum(const char* s) {
    if (*s == '$') s++;
    uint8_t c = 0;
    while (*s && *s != '*') c ^= static_cast<uint8_t>(*s++);
    return c;
}

// Appends "*HH" to a sentence already in `buf`. Returns the new length, or 0
// if there was not room -- the caller must check, because a truncated sentence
// with a valid-looking checksum is worse than no sentence.
inline uint8_t appendChecksum(char* buf, uint8_t capacity) {
    const size_t len = strlen(buf);
    if (len + 4 > capacity) return 0;
    const uint8_t ck = nmeaChecksum(buf);
    static const char kHex[] = "0123456789ABCDEF";
    buf[len]     = '*';
    buf[len + 1] = kHex[(ck >> 4) & 0x0F];
    buf[len + 2] = kHex[ck & 0x0F];
    buf[len + 3] = '\0';
    return static_cast<uint8_t>(len + 3);
}

// Verifies a "$...*HH" sentence. Returns false on a missing or wrong checksum.
inline bool verifyChecksum(const char* s) {
    const char* star = strrchr(s, '*');
    if (!star || strlen(star) < 3) return false;
    const uint8_t want = static_cast<uint8_t>(strtol(star + 1, nullptr, 16));
    return nmeaChecksum(s) == want;
}

}  // namespace nmea
