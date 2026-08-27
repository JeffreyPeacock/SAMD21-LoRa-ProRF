#!/usr/bin/env bash
# Verify the host, the serial path and the board, and say what is running.
# Run this before believing any negative result from anything else.

set -euo pipefail
# shellcheck source=scripts/lib/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib/common.sh"

step "host tools"
for t in pio python git; do
    if command -v "$t" >/dev/null 2>&1; then
        ok "$t  $("$t" --version 2>&1 | head -1)"
    else
        warn "$t  MISSING"
    fi
done
# Captured rather than piped into `grep -q`. Under `set -o pipefail`, grep -q
# exits as soon as it matches, pio dies of SIGPIPE, and the pipeline reports
# failure precisely when the match SUCCEEDED -- so the piped form says "not
# installed" about a platform that is installed.
PLATFORMS="$(pio pkg list -g --only-platforms 2>/dev/null || true)"
if [[ "$PLATFORMS" == *atmelsam* ]]; then
    ok "platform atmelsam installed"
else
    warn "platform atmelsam NOT installed -- run: pio pkg install -g -p platformio/atmelsam"
fi

step "USB"
USB="$(lsusb 2>/dev/null | grep -i '1b4f:21[45]f' || true)"
# Note both the sketch and the bootloader report 1b4f:214f on this board; the
# 215f half of that pattern has never been seen and is kept only in case a
# different revision uses it.
if [[ -n "$USB" ]]; then
    while read -r l; do ok "$l"; done <<<"$USB"
else
    fail "no SparkFun 1b4f:214f / 1b4f:215f on the USB bus.
  A charge-only cable enumerates nothing at all. Try another cable first."
fi

step "serial port"
PORT="$(require_port)"
ok "$PORT"
dim "  -> $(readlink -f "$PORT")"

# The bootloader is identified by the ABSENCE of a USB serial number, not by a
# different PID -- both report 214f. SAM-BA does not read the chip UID.
if ! udevadm info -q property -n "$PORT" 2>/dev/null | grep -q '^ID_SERIAL_SHORT='; then
    warn "the board is in its SAM-BA BOOTLOADER, not running a sketch."
    warn "that is the right state to upload into; it is not a fault."
    warn "press RESET to run the sketch again."
fi

step "permissions"
if [[ -r "$PORT" && -w "$PORT" ]]; then
    ok "readable and writable by $(id -un)"
else
    warn "no access to $PORT. Group: $(stat -c '%G' "$(readlink -f "$PORT")")"
    warn "add yourself with: sudo usermod -aG dialout $(id -un)   (then log out and in)"
fi

USERS="$(port_users "$PORT")"
if [[ -n "$USERS" ]]; then
    warn "something already has the port:"
    printf '%s\n' "$USERS" >&2
else
    ok "port is free"
fi

step "listening for 6 s"
dim "  (the SAMD21's USB CDC ignores baud rate; 115200 is nominal)"
# Heredoc is fine HERE because this only reads the serial port. Do not add
# anything that reads the terminal: `python - <<'PY'` makes the script itself
# stdin, so sys.stdin is not a tty and nothing typed can reach it. If this ever
# needs keyboard input, move it to scripts/lib/ like capture.py.
python - "$PORT" <<'PY'
import sys, time
import serial
port = sys.argv[1]
try:
    p = serial.Serial(port, 115200, timeout=0.2)
except Exception as e:
    print(f"  could not open: {e}")
    sys.exit(0)
p.dtr = True
deadline = time.time() + 6
data = b''
while time.time() < deadline:
    data += p.read(512)
p.close()
if not data:
    print("  nothing. Either no sketch is running, the sketch does not print,")
    print("  or it hung in setup() before its first output. Upload `probe`:")
    print("      pio run -e probe -t upload && ./scripts/monitor.sh")
else:
    text = data.decode('utf-8', 'replace')
    for line in text.splitlines():
        print(f"  | {line}")
PY
