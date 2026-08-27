// Host-side tests for lib/nmea: fixed-point float formatting and NMEA
// checksums. No board, no Arduino.h, runs in milliseconds.
//
// These are exactly the functions where an off-by-one is invisible on an
// oscilloscope and obvious here.

#include <unity.h>
#include <string.h>

#include <nmea.h>

void setUp(void) {}
void tearDown(void) {}

// ------------------------------------------------------------ formatting ---

static void expect(float v, uint8_t decimals, const char* want) {
    char buf[24];
    const uint8_t n = nmea::f(buf, v, decimals);
    TEST_ASSERT_EQUAL_STRING(want, buf);
    TEST_ASSERT_EQUAL_UINT8(strlen(want), n);
}

void test_format_basic(void) {
    expect(0.0f,      2, "0.00");
    expect(1.0f,      2, "1.00");
    expect(29.25f,    2, "29.25");
    expect(797.81f,   2, "797.81");
    expect(1970.5f,   1, "1970.5");
    expect(42.0f,     0, "42");
}

void test_format_negative(void) {
    expect(-1.0f,     2, "-1.00");
    expect(-139.22f,  2, "-139.22");
    expect(-0.5f,     1, "-0.5");
}

// The one that matters for a comma-separated record: a value that rounds up
// into the next integer must not emit "1.100" or "0.100".
void test_format_rounding_carry(void) {
    expect(0.999f,    2, "1.00");
    expect(1.995f,    2, "2.00");
    expect(9.99f,     1, "10.0");
    expect(-0.999f,   2, "-1.00");
}

void test_format_precision(void) {
    expect(1.5f,      4, "1.5000");
    expect(0.0001f,   4, "0.0001");
}

// Non-finite values must produce "nan", never a partial number -- a truncated
// digit run would corrupt every field after it in the sentence.
void test_format_non_finite(void) {
    char buf[24];
    nmea::f(buf, 0.0f / 0.0f, 2);
    TEST_ASSERT_EQUAL_STRING("nan", buf);
    nmea::f(buf, 1.0f / 0.0f, 2);
    TEST_ASSERT_EQUAL_STRING("nan", buf);
}

// ------------------------------------------------------------- checksums ---

// EXTERNAL TEST VECTORS, and that is the whole point of them. These three are
// the canonical NMEA 0183 example sentences -- they appear verbatim, with
// these checksums, in the reference material and in every parser's test suite.
//
// Checking against someone else's published sentences rather than against our
// own output is what makes this a real test: an implementation that is merely
// self-consistent passes a round-trip and fails this.
//
// They are reference data, not data from this project. The coordinates in them
// are the standard example values and describe nothing here.
void test_checksum_matches_published_sentences(void) {
    TEST_ASSERT_EQUAL_HEX8(0x47, nmea::nmeaChecksum(
        "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,"));
    TEST_ASSERT_EQUAL_HEX8(0x39, nmea::nmeaChecksum(
        "$GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1"));
    TEST_ASSERT_EQUAL_HEX8(0x6A, nmea::nmeaChecksum(
        "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W"));
}

void test_checksum_stops_at_the_star(void) {
    // A sentence that already carries "*HH" must checksum to the same value as
    // one that does not -- otherwise verifyChecksum() could never agree with
    // appendChecksum().
    TEST_ASSERT_EQUAL_HEX8(nmea::nmeaChecksum("$PJPT,1,2,3"),
                           nmea::nmeaChecksum("$PJPT,1,2,3*7F"));
}

void test_leading_dollar_is_optional(void) {
    TEST_ASSERT_EQUAL_HEX8(nmea::nmeaChecksum("$PJPT,1"),
                           nmea::nmeaChecksum("PJPT,1"));
}

void test_append_and_verify_roundtrip(void) {
    char buf[64];
    strcpy(buf, "$PJPT,1,2,3");
    const uint8_t n = nmea::appendChecksum(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT8(strlen(buf), n);
    TEST_ASSERT_EQUAL_CHAR('*', buf[strlen(buf) - 3]);
    TEST_ASSERT_TRUE(nmea::verifyChecksum(buf));
}

void test_verify_rejects_corruption(void) {
    char buf[64];
    strcpy(buf, "$PJPT,1,2,3");
    nmea::appendChecksum(buf, sizeof(buf));
    buf[6] = (buf[6] == '9') ? '8' : '9';       // flip a payload character
    TEST_ASSERT_FALSE(nmea::verifyChecksum(buf));
}

void test_verify_rejects_missing_checksum(void) {
    TEST_ASSERT_FALSE(nmea::verifyChecksum("$PJPT,1,2,3"));
    TEST_ASSERT_FALSE(nmea::verifyChecksum("$PJPT,1,2,3*"));
    TEST_ASSERT_FALSE(nmea::verifyChecksum("$PJPT,1,2,3*A"));
}

// A short sentence with a valid-looking checksum is worse than no sentence,
// so appendChecksum must refuse rather than truncate.
void test_append_refuses_when_it_will_not_fit(void) {
    char buf[8];
    strcpy(buf, "$PJPT");           // 5 chars + NUL; "*HH" needs 3 more + NUL
    TEST_ASSERT_EQUAL_UINT8(0, nmea::appendChecksum(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("$PJPT", buf);   // left untouched
}

// The 2019 firmware used a DIFFERENT rule -- XOR including the '$', printed in
// decimal. This asserts we do not accidentally drift back to it, because the
// two formats look alike and are not interchangeable.
void test_not_the_2019_rule(void) {
    char buf[64];
    strcpy(buf, "$XXWTH,2380.64,28.98,797.78,29.63");
    nmea::appendChecksum(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("$XXWTH,2380.64,28.98,797.78,29.63*73", buf);
    // The 2019 firmware printed "*87" for this exact sentence.
    TEST_ASSERT_NULL(strstr(buf, "*87"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_format_basic);
    RUN_TEST(test_format_negative);
    RUN_TEST(test_format_rounding_carry);
    RUN_TEST(test_format_precision);
    RUN_TEST(test_format_non_finite);
    RUN_TEST(test_checksum_matches_published_sentences);
    RUN_TEST(test_checksum_stops_at_the_star);
    RUN_TEST(test_leading_dollar_is_optional);
    RUN_TEST(test_append_and_verify_roundtrip);
    RUN_TEST(test_verify_rejects_corruption);
    RUN_TEST(test_verify_rejects_missing_checksum);
    RUN_TEST(test_append_refuses_when_it_will_not_fit);
    RUN_TEST(test_not_the_2019_rule);
    return UNITY_END();
}
