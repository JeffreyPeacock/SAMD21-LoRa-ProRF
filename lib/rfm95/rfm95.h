// RFM95W bring-up, in one place.
//
// RadioHead does the protocol work; what it does NOT do is touch the reset
// line. RH_RF95 takes only a chip-select and an interrupt pin, so `init()`
// starts from whatever mode the previous run left the SX1276 in -- which on a
// warm restart is not the reset state. Every program here pulses RST first,
// and doing it in three separate places was two places too many.

#pragma once

#include <RH_RF95.h>

namespace rfm95 {

// Pulses RST, calls init(), sets the frequency and transmit power from
// board.h, and reports what happened on SerialUSB. Returns false if the radio
// did not answer -- the caller decides whether that is fatal. Nothing here
// calls while(1).
bool begin(RH_RF95& radio);

// Same, with an explicit power in dBm (2..20 on PA_BOOST). Use a low value for
// bench work: two radios a metre apart at 14 dBm can saturate the receiver,
// and the symptom looks like a bug rather than overload.
bool begin(RH_RF95& radio, uint8_t txPowerDbm);

// The hardware reset on its own. RST is active low; the SX1276 wants >100 us
// low and ~5 ms to settle.
void reset();

}  // namespace rfm95
