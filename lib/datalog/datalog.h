// Qwiic OpenLog, made optional.
//
// The SD card is the record of record: the radio link is expected to drop
// packets and the log is what fills the gaps in afterwards. So logging must
// never depend on the radio, and the radio must never wait for the log.
//
// If the OpenLog is absent, every call here becomes a no-op and the node keeps
// running. `present()` says which happened, so the banner can be honest about
// whether anything is being written.

#pragma once

#include <stdint.h>

namespace datalog {

// `filename` is 8.3 -- the OpenLog's firmware is FAT short names only, and a
// longer name is silently mangled rather than rejected.
bool begin(const char* filename);

bool present();

// One line, newline appended. Buffered by the OpenLog itself.
void line(const char* s);

// Pushes the OpenLog's buffer to the card. Data written but not synced is lost
// if power drops, and this node is battery powered and carried around, so sync
// on a timer rather than at shutdown -- there is no shutdown.
void sync();

}  // namespace datalog
