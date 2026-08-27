#!/usr/bin/env bash
# Set the board's clock from this desktop.
#
#     ./scripts/set-clock.sh
#
# The Pro RF has no RTC crystal and no backup cell, so it boots knowing nothing
# but milliseconds since reset. This script supplies real time from the host,
# which is the only source the board has.
#
# The SAMD21's RTC keeps running across a reset while the rail stays up, and
# reflashing does not drop the rail. So this survives uploads and only needs
# doing once per power cycle.

set -euo pipefail
# shellcheck source=scripts/lib/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib/common.sh"

PORT="$(require_port)"
lock_port "$PORT"

EPOCH="$(date -u +%s)"
step "sending UTC $(date -u -d "@$EPOCH" '+%Y-%m-%d %H:%M:%S') to $PORT"

PRORF_PORT="$PORT" PRORF_EPOCH="$EPOCH" python - <<'PY'
import os, time, serial
port = os.environ["PRORF_PORT"]
epoch = os.environ["PRORF_EPOCH"]
s = serial.Serial(port, 115200, timeout=0.3)
s.dtr = True
time.sleep(0.3)
s.reset_input_buffer()
# Sent AFTER the port settles: anything written before the CDC is up is
# discarded by the Arduino SAMD core with no error.
s.write(f"T{epoch}\n".encode())
s.flush()
deadline = time.time() + 3
out = b""
while time.time() < deadline:
    out += s.read(256)
s.close()
text = out.decode("utf-8", "replace")
for line in text.splitlines():
    if "clock" in line:
        print(f"  {line.strip()}")
if "clock" not in text:
    print("  no acknowledgement -- is a sketch running that calls rtc::pollSerial()?")
PY
