#!/usr/bin/env bash
# Watch the kernel log for a board plugging in, and say what happened.
#
#     ./scripts/usb-watch.sh [seconds]     default 60
#
# "The system sees a USB device but no ACM device is created" covers several
# different faults with different fixes, and only the exact kernel lines tell
# them apart. This captures them and classifies the result.

set -euo pipefail
# shellcheck source=scripts/lib/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib/common.sh"

DURATION="${1:-60}"

step "watching the kernel log for ${DURATION}s"
dim "  plug the board in now, or press RESET twice quickly for the bootloader"
echo

BEFORE="$(mktemp)"
(journalctl -k --no-pager -n 1 -o short-unix 2>/dev/null || true) | tail -1 > "$BEFORE"

timeout "$DURATION" journalctl -k -f -o short 2>/dev/null \
  | grep --line-buffered -iE "usb |cdc_acm|unable to enumerate|descriptor|not accepting" \
  | tee /tmp/usb-watch.log || true

echo
step "what that means"

LOG="$(cat /tmp/usb-watch.log 2>/dev/null || true)"

if [[ -z "$LOG" ]]; then
    warn "nothing at all. The board is not being detected electrically."
    echo "  * try a different cable -- a CHARGE-ONLY cable produces exactly this"
    echo "  * try a different port, ideally straight into the machine"
    exit 0
fi

if grep -qiE "device descriptor read|unable to enumerate|error -(71|32|110)" <<<"$LOG"; then
    warn "ENUMERATION FAILED -- the host could not read the device's descriptors."
    echo "  The board is drawing power and trying to talk, and the conversation"
    echo "  is breaking down. In order of likelihood:"
    echo "    * cable (try another -- this is the most common cause by far)"
    echo "    * a port that cannot supply enough current; avoid hubs"
    echo "    * firmware crashing during USB init -- try the double-tap below"
    echo "    * damaged USB circuitry on the board"
elif grep -qi "cdc_acm" <<<"$LOG"; then
    ok "a CDC ACM device WAS created -- the board is fine at the USB layer"
    if grep -qi "Arduino LLC" <<<"$LOG"; then
        ok "and it is in its SAM-BA BOOTLOADER (Manufacturer: Arduino LLC, no serial)"
        echo "  Upload to it now:   make flash-probe"
    else
        ok "and it is running a sketch (Manufacturer: SparkFun, serial present)"
    fi
elif grep -qiE "New USB device found" <<<"$LOG"; then
    warn "the device ENUMERATED but no cdc_acm interface was bound."
    echo "  Descriptors were read, so the USB hardware works. What is running on"
    echo "  it is not presenting a working CDC interface."
    echo "  * DOUBLE-TAP RESET. The bootloader is a separate program in protected"
    echo "    flash and presents its own CDC -- if that appears, the chip is fine"
    echo "    and only the application is broken."
else
    warn "unclassified. The raw lines are above and in /tmp/usb-watch.log"
fi

echo
dim "For reference, a HEALTHY Pro RF on this machine looks like:"
dim "  running a sketch : Manufacturer SparkFun,    Product 'SFE SAMD21 Pro RF',      serial present"
dim "  bootloader       : Manufacturer 'Arduino LLC', Product 'SparkFun SAMD21 Pro RF', NO serial"
dim "Both create a ttyACM. Only the second accepts an upload."
