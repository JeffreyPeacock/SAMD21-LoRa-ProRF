#!/usr/bin/env bash
# Open a serial monitor on the board, whichever ttyACM it landed on.
# Ctrl-C exits.
#
# NOT miniterm, and the reason is worth keeping. miniterm puts the terminal in
# RAW mode, which disables ISIG -- so Ctrl-C stops being a signal and becomes
# an ordinary byte that the application has to notice and act on. Its
# --exit-char 3 makes it act on that byte, and that DID pass a pty test here,
# and it still left the monitor unquittable in a real session by either
# Ctrl-C or Ctrl-]. When the only way out of a program depends on that
# program's own input loop, a wedged input loop traps you.
#
# lib/capture.py uses cbreak instead. cbreak leaves ISIG enabled, so the
# KERNEL delivers SIGINT on Ctrl-C regardless of what the program is doing,
# and keystrokes still reach the board -- which is the property miniterm was
# here for in the first place.

set -euo pipefail
# shellcheck source=scripts/lib/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib/common.sh"

# How long to wait for the board to appear. Short by default -- standing here
# for 15 s to be told the board is in its bootloader is not useful. flash.sh
# raises it, because straight after an upload the board really is still
# re-enumerating and the wait is the point.
WAIT="${MONITOR_WAIT:-3}"

if ! PORT="$(wait_for_app_port "$WAIT")"; then
    if find_port >/dev/null; then
        fail "the board is in its SAM-BA bootloader, not running a sketch.
  A monitor would sit here silently forever. Press RESET, or upload something:
      make flash APP=probe"
    fi
    fail "no Pro RF found. Check the cable is a DATA cable, then:
      ls -l /dev/serial/by-id/"
fi

lock_port "$PORT"
step "monitoring $PORT   (Ctrl-C to exit)"
dim "  keystrokes go to the board -- the probe tool re-runs on any key"
exec python "$(dirname "${BASH_SOURCE[0]}")/lib/capture.py" "$PORT" 0 --plain
