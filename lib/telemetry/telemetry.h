// The wire format between node and base.
//
// Design notes, because the shape of this is a direct response to what the
// 2019 sketches got wrong:
//
// * The old pair was synchronous: the node sent, then blocked in
//   waitAvailableTimeout() until the base answered. One lost ACK stalled the
//   whole node for the length of the timeout, and sensor sampling stopped with
//   it. Here the node never blocks on the base. It transmits, listens briefly
//   in whatever time is left before the next sample, and carries on. An ACK
//   that never arrives costs nothing.
//
// * Every packet carries a 32-bit sequence number that only ever increases.
//   The base does not need the packets to arrive in order or at all -- it
//   reports the highest sequence it has seen and how many are missing, so a
//   gap is visible immediately rather than inferred later from the SD card.
//
// * The ACK carries the base's RSSI and SNR for the packet being acknowledged.
//   That is the only way the node can learn its own link quality: a transmitter
//   cannot measure how well it was heard.
//
// * Fixed-size, little-endian, packed structs. Both ends are the same SAMD21,
//   so there is no endianness or alignment question today -- but writing it
//   down is what lets a third radio (the ESP32 gateway) decode these without
//   guessing. See docs/link-protocol.md.

#pragma once

#include <stdint.h>

namespace telemetry {

constexpr uint8_t kMagic   = 0x4A;  // 'J'
constexpr uint8_t kVersion = 1;

enum PacketType : uint8_t {
    kTypeTelemetry = 1,  // node -> base, sensor record
    kTypeAck       = 2,  // base -> node, receipt + link quality
    kTypeHello     = 3,  // node -> base, sent once at startup
};

// RH_RF95_MAX_MESSAGE_LEN is 251: the SX1276's 255-byte FIFO less RadioHead's
// 4-byte header (to, from, id, flags). Everything below has to fit in that.
constexpr uint8_t kMaxMessageLen = 251;

#pragma pack(push, 1)

struct Header {
    uint8_t  magic;    // kMagic
    uint8_t  version;  // kVersion
    uint8_t  type;     // PacketType
    uint8_t  nodeId;   // which node sent it; 1 for the only one we have
    uint32_t seq;      // monotonic, never reused, survives no reboot
};
static_assert(sizeof(Header) == 8, "Header must be 8 bytes on the wire");

// Telemetry payload is an ASCII record, not more binary fields. It is the same
// NMEA-style sentence written to the SD card, so what is logged and what is
// sent are byte-identical and a capture from either end is directly
// comparable. Costs bandwidth; buys the ability to read a packet with your
// eyes at 3am. A full telemetry record is ~150 bytes, which with RadioHead's
// 4-byte header is 256 ms of airtime at SF7/BW125 -- 8.5 % of a 3 s transmit
// interval. See docs/link-protocol.md for the whole table.
constexpr uint8_t kMaxRecordLen = kMaxMessageLen - sizeof(Header) - 1;  // 242

struct Telemetry {
    Header header;
    char   record[kMaxRecordLen + 1];  // NUL-terminated; only strlen+1 is sent
};

struct Ack {
    Header   header;    // seq = the sequence being acknowledged
    uint32_t highest;   // highest sequence the base has seen from this node
    uint32_t received;  // how many distinct packets the base has accepted
    int16_t  rssi;      // base-side RSSI, dBm, of the acknowledged packet
    int8_t   snr;       // base-side SNR, dB
    uint8_t  reserved;
};
static_assert(sizeof(Ack) == 20, "Ack must be 20 bytes on the wire");

#pragma pack(pop)

// Byte count actually put on the air for a telemetry packet holding `record`.
inline uint8_t telemetryLen(uint8_t recordLen) {
    return static_cast<uint8_t>(sizeof(Header) + recordLen + 1);
}

inline bool headerLooksValid(const uint8_t* buf, uint8_t len) {
    if (len < sizeof(Header)) return false;
    const Header* h = reinterpret_cast<const Header*>(buf);
    return h->magic == kMagic && h->version == kVersion;
}

}  // namespace telemetry
