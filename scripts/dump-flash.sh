#!/usr/bin/env bash
# Read the firmware off the board.
#
#   ./scripts/dump-flash.sh [outfile]        default etc/firmware/prorf-flash-dump.bin
#
# Two reasons to run this:
#
#   1. It is the only backup that exists. The source for whatever is currently
#      flashed is not on this machine. Once the
#      board is reflashed that image is gone for good.
#   2. The strings in it say what the sketch does. Every Serial print, every
#      format string, and the library version banners survive compilation, and
#      that is enough to reconstruct the structure of a lost sketch.
#
# How it gets into the bootloader: the same 1200-baud touch PlatformIO uses.
# That is handled by the RUNNING sketch's USB stack, so it works only while the
# sketch is alive. If the board is hung, double-tap RESET first and pass the
# bootloader port explicitly with BOOTLOADER_PORT=/dev/ttyACMn.
#
# The board re-enumerates when it enters the bootloader, but NOT the way the
# PlatformIO board definition implies. Measured on this board 2026-08-21:
#
#   running a sketch   1b4f:214f  "SparkFun" / "SFE SAMD21 Pro RF"  serial present
#   SAM-BA bootloader  1b4f:214f  "Arduino LLC" / "SparkFun SAMD21 Pro RF"  NO serial
#
# The PID does NOT change, and the tty usually comes back on the same node. So
# waiting for a *new* /dev/ttyACMn never fires. What does change is the USB
# manufacturer string and the serial number -- the bootloader has none, because
# it does not read the chip UID. That is what this waits on.

set -euo pipefail
# shellcheck source=scripts/lib/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib/common.sh"

ROOT="$(repo_root)"
OUT="${1:-$ROOT/etc/firmware/prorf-flash-dump.bin}"
BOSSAC="${BOSSAC:-$HOME/.platformio/packages/tool-bossac/bossac}"

[[ -x "$BOSSAC" ]] || fail "bossac not found at $BOSSAC
  install it with:  pio pkg install -g -t platformio/tool-bossac"

mkdir -p "$(dirname "$OUT")"

# Snapshot the ttys that exist now, so the new one can be identified by
# difference rather than by guessing at a name.

if [[ -n "${BOOTLOADER_PORT:-}" ]]; then
    BPORT="$BOOTLOADER_PORT"
    step "using bootloader port given on the command line: $BPORT"
else
    PORT="$(require_port)"
    step "found the board at $PORT"

    step "knocking it into the bootloader (1200-baud touch)"
    python - "$PORT" <<'PY'
import sys, time, serial
port = sys.argv[1]
try:
    s = serial.Serial(port, 1200)
    s.dtr = False          # the touch is 1200 baud with DTR deasserted
    time.sleep(0.1)
    s.close()
except Exception as e:
    # The port vanishing mid-close IS the success case on a native-USB board.
    print(f"  (port closed with {type(e).__name__} -- expected)")
time.sleep(0.5)
PY

    step "waiting for the bootloader to enumerate"
    BPORT=""
    for _ in $(seq 1 100); do
        for cand in /dev/ttyACM*; do
            [[ -e "$cand" ]] || continue
            # The bootloader is the one with no USB serial number. A running
            # sketch reports the SAMD21's 128-bit chip UID; SAM-BA does not.
            if ! udevadm info -q property -n "$cand" 2>/dev/null \
                 | grep -q '^ID_SERIAL_SHORT='; then
                BPORT="$cand"; break 2
            fi
        done
        sleep 0.1
    done
    [[ -n "$BPORT" ]] || fail "no port without a USB serial number appeared, so
  nothing entered the bootloader. The sketch may be hung and unable to answer
  the touch. Double-tap RESET, confirm with

      udevadm info -q property -n /dev/ttyACM0 | grep -E 'ID_VENDOR=|ID_SERIAL_SHORT='

  -- the bootloader shows ID_VENDOR=Arduino_LLC and NO ID_SERIAL_SHORT -- then:
      BOOTLOADER_PORT=/dev/ttyACM0 $0 $*"
    ok "bootloader on $BPORT"
fi

step "device info"
"$BOSSAC" -p "$BPORT" -i || fail "bossac could not talk to $BPORT.
  If the board is not in its bootloader, double-tap RESET and pass
  BOOTLOADER_PORT explicitly."

step "reading 256 KB of flash -> $OUT"
# Whole flash, bootloader included. If BOOTPROT blocks the first 8 KB it comes
# back as zeros rather than failing, which is worth seeing in the dump.
if ! "$BOSSAC" -p "$BPORT" -o 0x0000 -r262144 "$OUT"; then
    warn "full read failed; retrying from the application offset 0x2000"
    "$BOSSAC" -p "$BPORT" -o 0x2000 -r253952 "$OUT" \
        || fail "could not read the flash"
fi

ok "$(stat -c%s "$OUT") bytes -> $OUT"
dim "  sha256: $(sha256sum "$OUT" | cut -d' ' -f1)"

step "strings that look like sketch output"
# Printable runs of 6+ chars. The filter keeps what a person wrote and drops
# the toolchain's own noise.
strings -n 6 "$OUT" \
  | grep -vE '^(GCC|gcc|\.\./|/usr/|/home/|__|_Z|arm-none)' \
  | grep -E '[A-Za-z]{3}' \
  | head -120

echo
dim "  full list:  strings -n 6 '$OUT' | less"
dim "  the board is still in its bootloader -- press RESET to run the sketch again,"
dim "  or upload something with ./scripts/flash.sh"
