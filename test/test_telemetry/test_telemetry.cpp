// Host-side tests for lib/telemetry: the on-air frame layout.
//
// These assert the wire format, which is the one thing a second implementation
// (the ESP32 gateway) depends on. If a field moves, this fails here rather
// than as silent garbage on the far radio.

#include <unity.h>
#include <string.h>

#include <telemetry.h>

void setUp(void) {}
void tearDown(void) {}

void test_header_layout(void) {
    TEST_ASSERT_EQUAL_UINT(8, sizeof(telemetry::Header));

    telemetry::Header h{};
    uint8_t* base = reinterpret_cast<uint8_t*>(&h);
    TEST_ASSERT_EQUAL_UINT(0, reinterpret_cast<uint8_t*>(&h.magic)   - base);
    TEST_ASSERT_EQUAL_UINT(1, reinterpret_cast<uint8_t*>(&h.version) - base);
    TEST_ASSERT_EQUAL_UINT(2, reinterpret_cast<uint8_t*>(&h.type)    - base);
    TEST_ASSERT_EQUAL_UINT(3, reinterpret_cast<uint8_t*>(&h.nodeId)  - base);
    TEST_ASSERT_EQUAL_UINT(4, reinterpret_cast<uint8_t*>(&h.seq)     - base);
}

void test_seq_is_little_endian(void) {
    telemetry::Header h{};
    h.seq = 0x11223344UL;
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&h);
    TEST_ASSERT_EQUAL_HEX8(0x44, b[4]);
    TEST_ASSERT_EQUAL_HEX8(0x33, b[5]);
    TEST_ASSERT_EQUAL_HEX8(0x22, b[6]);
    TEST_ASSERT_EQUAL_HEX8(0x11, b[7]);
}

void test_ack_layout(void) {
    TEST_ASSERT_EQUAL_UINT(20, sizeof(telemetry::Ack));
    telemetry::Ack a{};
    uint8_t* base = reinterpret_cast<uint8_t*>(&a);
    TEST_ASSERT_EQUAL_UINT( 8, reinterpret_cast<uint8_t*>(&a.highest)  - base);
    TEST_ASSERT_EQUAL_UINT(12, reinterpret_cast<uint8_t*>(&a.received) - base);
    TEST_ASSERT_EQUAL_UINT(16, reinterpret_cast<uint8_t*>(&a.rssi)     - base);
    TEST_ASSERT_EQUAL_UINT(18, reinterpret_cast<uint8_t*>(&a.snr)      - base);
}

// RH_RF95_MAX_MESSAGE_LEN is 251: the SX1276's 255-byte FIFO less RadioHead's
// 4-byte to/from/id/flags header. Nothing may exceed it.
void test_everything_fits_in_a_radiohead_payload(void) {
    TEST_ASSERT_EQUAL_UINT(251, telemetry::kMaxMessageLen);
    TEST_ASSERT_EQUAL_UINT(242, telemetry::kMaxRecordLen);
    TEST_ASSERT_TRUE(sizeof(telemetry::Ack) <= telemetry::kMaxMessageLen);
    TEST_ASSERT_EQUAL_UINT(telemetry::kMaxMessageLen,
                           telemetry::telemetryLen(telemetry::kMaxRecordLen));
}

void test_telemetry_len_counts_the_nul(void) {
    // 8 header + n record + 1 terminator.
    TEST_ASSERT_EQUAL_UINT8(9,  telemetry::telemetryLen(0));
    TEST_ASSERT_EQUAL_UINT8(19, telemetry::telemetryLen(10));
}

void test_header_validation(void) {
    uint8_t buf[16] = {0};
    TEST_ASSERT_FALSE(telemetry::headerLooksValid(buf, 7));   // too short

    telemetry::Header h{};
    h.magic = telemetry::kMagic;
    h.version = telemetry::kVersion;
    memcpy(buf, &h, sizeof(h));
    TEST_ASSERT_TRUE(telemetry::headerLooksValid(buf, sizeof(h)));

    buf[0] = 0x00;
    TEST_ASSERT_FALSE(telemetry::headerLooksValid(buf, sizeof(h)));
    buf[0] = telemetry::kMagic;
    buf[1] = 0x99;                                            // wrong version
    TEST_ASSERT_FALSE(telemetry::headerLooksValid(buf, sizeof(h)));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_header_layout);
    RUN_TEST(test_seq_is_little_endian);
    RUN_TEST(test_ack_layout);
    RUN_TEST(test_everything_fits_in_a_radiohead_payload);
    RUN_TEST(test_telemetry_len_counts_the_nul);
    RUN_TEST(test_header_validation);
    return UNITY_END();
}
