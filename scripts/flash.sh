#!/usr/bin/env bash
# Build and upload one environment.
#
#   ./scripts/flash.sh probe|node|base [--monitor]
#
# Uploading uses the 1200-baud touch to drop the board into its SAM-BA
# bootloader. That only works if the running sketch's USB stack is alive; if
# the board is hung, double-tap RESET by hand and run this again.

set -euo pipefail
# shellcheck source=scripts/lib/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib/common.sh"

ROOT="$(repo_root)"

# Read the valid targets out of platformio.ini rather than hardcoding them.
# The hardcoded list went stale the moment tools were added, and rejected
# environments that build perfectly well.
mapfile -t ENVS < <(grep -oP '^\[env:\K[^\]]+' "$ROOT/platformio.ini" | grep -v '^native$')

ENV="${1:-}"
if [[ -z "$ENV" ]] || ! printf '%s\n' "${ENVS[@]}" | grep -qx -- "$ENV"; then
    fail "usage: $0 <target> [--monitor]
  targets: ${ENVS[*]}"
fi

PORT="$(require_port)"
lock_port "$PORT"

step "building $ENV"
pio run -e "$ENV" >/dev/null || fail "build failed -- run 'pio run -e $ENV' to see why"
ok "built"

# DO THE TOUCH OURSELVES, then hand pio an explicit port.
#
# Letting pio auto-detect was correct with ONE board: the 1200-baud touch makes
# the running sketch's by-id path vanish -- it is built from the chip UID,
# which SAM-BA does not report -- so an explicit by-id path waits forever for a
# name that never returns.
#
# With TWO identical boards attached, auto-detect became a coin flip, and it
# flashed the wrong one. The fix is not to go back to a by-id path, but to
# touch the chosen board ourselves and then pass the RAW device node, which
# does survive the re-enumeration.
NODE="$(resolve_node "$PORT")"
step "resetting $NODE into its bootloader (BOARD=$BOARD)"

PRORF_TOUCH_PORT="$NODE" python - <<'TOUCH'
import os, time, serial
port = os.environ["PRORF_TOUCH_PORT"]
try:
    s = serial.Serial(port, 1200)
    s.dtr = False          # 1200 baud with DTR deasserted IS the command
    time.sleep(0.1)
    s.close()
except Exception as exc:
    # The port vanishing mid-close is the success case on native USB.
    print(f"  (closed with {type(exc).__name__} -- expected)")
TOUCH

# Wait for a bootloader: a port with NO USB serial number, because SAM-BA does
# not read the chip UID. Prefer the same device node, which is what this board
# normally returns on.
BOOTPORT=""
for _ in $(seq 1 100); do
    for cand in /dev/ttyACM*; do
        [[ -e "$cand" ]] || continue
        if ! udevadm info -q property -n "$cand" 2>/dev/null | grep -q '^ID_SERIAL_SHORT='; then
            BOOTPORT="$cand"
            [[ "$cand" == "$NODE" ]] && break 2
        fi
    done
    [[ -n "$BOOTPORT" ]] && break
    sleep 0.1
done

if [[ -z "$BOOTPORT" ]]; then
    fail "no bootloader appeared after the touch on $NODE.
  A hung sketch cannot answer the touch. Double-tap RESET on the board you
  mean, then re-run. Attached now:
$(list_boards)"
fi
ok "bootloader on $BOOTPORT"

step "uploading to $BOOTPORT"
flock -u "$LOCK_FD"
if ! pio run -e "$ENV" -t upload --upload-port "$BOOTPORT"; then
    fail "upload failed.
  If the board is hung, its USB stack cannot answer the 1200-baud touch.
  Double-tap RESET -- the LED pulses slowly and the board waits for bossac --
  and run this again. (The USB PID stays 214f in the bootloader; what changes
  is the manufacturer string and the loss of the USB serial number.)"
fi
ok "uploaded"

if [[ "${2:-}" == "--monitor" ]]; then
    # Release the lock before handing over, or monitor.sh refuses its own
    # caller. It re-takes it once the board is back.
    flock -u "$LOCK_FD"
    step "waiting for the board to re-enumerate"
    MONITOR_WAIT=20 exec "$(dirname "${BASH_SOURCE[0]}")/monitor.sh"
fi
