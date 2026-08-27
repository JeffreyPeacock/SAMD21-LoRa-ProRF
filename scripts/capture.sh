#!/usr/bin/env bash
# Capture serial output across a reset, with keystrokes forwarded to the board.
#
#   ./scripts/capture.sh [seconds|forever]     default 90
#
# A plain serial monitor cannot watch a SAMD21 reboot. The SAMD21 *is* the USB
# device, so RESET tears down the connection: the tty vanishes, the monitor
# dies with an I/O error, and a new tty appears a second or two later. This
# reconnects as soon as the device node returns and keeps one continuous
# timestamped transcript across the gap.
#
# Keystrokes reach the board, so tools that re-run on a keypress (probe,
# i2c_scan) work here as well as under a plain monitor. Ctrl-C exits.
#
# What it still cannot capture is whatever the board prints between its own
# SerialUSB.begin() and the host opening the port: the Arduino SAMD core
# discards CDC writes while no host is attached. probe works around that by
# reprinting whenever a host attaches.

set -euo pipefail
# shellcheck source=scripts/lib/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib/common.sh"

DURATION="${1:-90}"
[[ "$DURATION" == "forever" || "$DURATION" == "-" ]] && DURATION=0

PORT="$(require_port)"
lock_port "$PORT"

if [[ "$DURATION" == "0" ]]; then
    step "capturing $PORT until Ctrl-C"
else
    step "capturing $PORT for ${DURATION}s"
fi
dim "  press RESET whenever you like -- resets are marked, not fatal"
dim "  keystrokes go to the board; Ctrl-C to stop"
dim "  (timestamps are seconds since this started)"

# NOT `python - <<'PY'`: a heredoc makes the script itself stdin, so the
# terminal is unreachable and keystroke forwarding silently does nothing.
exec python "$(dirname "${BASH_SOURCE[0]}")/lib/capture.py" "$PORT" "$DURATION"
