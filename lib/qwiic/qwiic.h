// The Qwiic chain as a slot scheduler.
//
// One shared I2C bus, five devices, read in a repeating cycle. Each device
// gets a slot long enough to read everything it has; a full cycle is N slots
// and then it starts again.
//
// TWO THINGS SHAPE THIS DESIGN, and both are properties of the hardware rather
// than preferences:
//
// 1. THE BUS IS SERIAL. I2C carries one transaction at a time, so "read the
//    devices in parallel" is not available at any price. What IS concurrent is
//    the devices' own conversion work -- the CCS811 runs a 1 Hz cycle
//    independently of anything we do -- so a slot collects what is already
//    ready instead of commanding and waiting.
//
// 2. NOTHING IN THE CYCLE MAY BLOCK. A device that is slow, wedged or absent
//    must cost its own slot and nothing else. Every read is bounded and every
//    failure is counted rather than retried until it succeeds.
//
// Every device is optional and every read is allowed to fail. A single failure
// is transient: it is counted and the cycle moves on. Only a RUN of failures
// is a defect, which is why consecutive failures are tracked separately --
// scattered singles and a dead device are indistinguishable from a total.

#pragma once

#include <stdint.h>

namespace qwiic {

enum DeviceId : uint8_t {
    kEnv = 0,   // BME280   temperature, humidity, pressure, altitude
    kAir,       // CCS811   eCO2, TVOC
    kDeviceCount
};

struct Stats {
    uint32_t attempts            = 0;
    uint32_t ok                  = 0;
    uint32_t failed              = 0;   // present, but produced nothing this slot
    uint32_t consecutiveFailures = 0;
    uint32_t worstStreak         = 0;
    uint32_t minMicros           = 0xFFFFFFFF;
    uint32_t maxMicros           = 0;
    uint64_t totalMicros         = 0;
};

struct Device {
    const char* name    = "";
    uint8_t     address = 0;
    bool        present = false;
    Stats       stats;
};

// Brings up whatever answers and reports it on SerialUSB. Never blocks
// forever, never calls while(1): a chain where nothing answers is a valid
// result and the caller carries on.
void begin();

const Device& device(DeviceId id);


// Read one device and format its sentence. Returns the length, or 0 if the
// device is absent or had nothing this slot -- which is not an error, only a
// gap. Sentence forms follow the 2019 reference:
//
//   $XXWTH,<altM>,<tempC>,<hPa>,<rh>*HH
//   $XXCO2,<ms>,<co2ppm>,<tvocppb>*HH
//
// Checksums are STANDARD NMEA -- XOR between '$' and '*', printed as %02X.
uint8_t readInto(DeviceId id, char* buf, uint8_t cap);


// One line of per-device counters, for the periodic health report.
uint8_t formatStats(DeviceId id, char* buf, uint8_t cap);

}  // namespace qwiic
