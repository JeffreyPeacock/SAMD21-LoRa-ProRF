# Shared helpers. Source this; do not execute it.
#
# shellcheck shell=bash

set -o pipefail

# ------------------------------------------------------------------ output --
if [[ -t 1 ]]; then
    C_RESET=$'\033[0m'; C_BOLD=$'\033[1m'; C_DIM=$'\033[2m'
    C_RED=$'\033[31m';  C_GREEN=$'\033[32m'; C_YELLOW=$'\033[33m'
else
    C_RESET=''; C_BOLD=''; C_DIM=''; C_RED=''; C_GREEN=''; C_YELLOW=''
fi

info() { printf '%s\n' "$*"; }
step() { printf '%s==>%s %s\n' "$C_BOLD" "$C_RESET" "$*"; }
ok()   { printf '%s  ok%s  %s\n' "$C_GREEN" "$C_RESET" "$*"; }
warn() { printf '%s warn%s %s\n' "$C_YELLOW" "$C_RESET" "$*" >&2; }
fail()    { printf '%s fail%s %s\n' "$C_RED" "$C_RESET" "$*" >&2; exit 1; }
section() { printf '\n== %s ==\n' "$*"; }

# FAILURES is a counter the caller maintains; report_result summarises it.
FAILURES="${FAILURES:-0}"
report_result() {
    section 'result'
    if [[ ${FAILURES} -eq 0 ]]; then ok 'all checks passed'; return 0; fi
    printf '%s fail%s %s\n' "$C_RED" "$C_RESET" "${FAILURES} check(s) failed" >&2
    return 1
}
dim()  { printf '%s%s%s\n' "$C_DIM" "$*" "$C_RESET"; }

# -------------------------------------------------------------------- port --
# Everything here matches *SAMD21_Pro_RF* and nothing else, by design. Other
# boards on this machine belong to other people and other projects:
# /dev/ttyUSB1 is an ESP32 gateway managed by another developer, /dev/ttyUSB0
# is a CP2102 belonging to a third. Do not widen these globs to "find a serial
# port" -- opening a tty asserts DTR and can reset whatever is on the far end.
# The SAMD21 carries a real USB serial number -- the chip's 128-bit unique ID --
# so udev can name it unambiguously. Prefer that over /dev/ttyACMn, which is
# handed out in plug order and moves when something else is connected.
#
# The board presents two different USB identities. Measured 2026-08-21 -- and
# note this contradicts the PlatformIO board definition, which lists 0x215f as
# a second hwid. That PID was never observed:
#
#   running a sketch   1b4f:214f  "SparkFun" / "SFE SAMD21 Pro RF"   serial PRESENT
#   SAM-BA bootloader  1b4f:214f  "Arduino LLC" / "SparkFun SAMD21 Pro RF"  NO serial
#
# So the PID is useless for telling them apart. The serial number is the
# reliable discriminator: SAM-BA does not read the chip UID, so its by-id path
# carries no serial and is named usb-Arduino_LLC_SparkFun_SAMD21_Pro_RF-if00.
# Both are matched here, because a board sitting in its bootloader is exactly
# when you most need to find its port.
# TWO BOARDS NOW, and they are the same model with the same VID:PID. The only
# thing that tells them apart is the SAMD21's own 128-bit chip UID, exposed as
# the USB serial number.
#
# This matters more than it looks. scripts/flash.sh stopped passing an explicit
# --upload-port so PlatformIO could rediscover the board after the 1200-baud
# touch -- and with two identical boards attached, that auto-detect is a coin
# flip. It picked the wrong one at least once.
PRORF_SENSOR_SERIAL="${PRORF_SENSOR_SERIAL:-21A8043C5150484735202020FF09181F}"
PRORF_BASE_SERIAL="${PRORF_BASE_SERIAL:-B56628F85150484735202020FF082C1A}"

# Which board a command acts on. Override per invocation: BOARD=base
BOARD="${BOARD:-sensor}"

case "$BOARD" in
    sensor) PRORF_SERIAL="${PRORF_SERIAL:-$PRORF_SENSOR_SERIAL}" ;;
    base)   PRORF_SERIAL="${PRORF_SERIAL:-$PRORF_BASE_SERIAL}" ;;
    *)      PRORF_SERIAL="${PRORF_SERIAL:-$BOARD}" ;;   # or a raw serial
esac

find_port() {
    local p
    for p in /dev/serial/by-id/*SAMD21_Pro_RF*"${PRORF_SERIAL}"*; do
        [[ -e "$p" ]] && { printf '%s\n' "$p"; return 0; }
    done
    # NO BLIND FALLBACK when more than one board is attached. Picking "any Pro
    # RF" was safe with one board and is actively dangerous with two: it flashes
    # whichever enumerated first.
    local count=0
    for p in /dev/serial/by-id/*SAMD21_Pro_RF*; do
        [[ -e "$p" ]] && count=$((count + 1))
    done
    if (( count > 1 )); then
        return 1
    fi
    for p in /dev/serial/by-id/*SAMD21_Pro_RF*; do
        [[ -e "$p" ]] && { printf '%s\n' "$p"; return 0; }
    done
    return 1
}

# The raw /dev/ttyACMn behind a by-id path. bossac needs a node that survives
# the 1200-baud touch, and the by-id name does not: it is built from the chip
# UID, and SAM-BA does not report one.
resolve_node() { readlink -f "$1"; }

# List every attached Pro RF, for an error message that is actually useful.
list_boards() {
    local p
    for p in /dev/serial/by-id/*SAMD21_Pro_RF*; do
        [[ -e "$p" ]] || continue
        printf '    %-12s %s\n' "$(readlink -f "$p")" "$(basename "$p")"
    done
}

# The board in its NORMAL state -- running a sketch, reporting the SAMD21's
# chip UID as a USB serial number. The bootloader has no serial, so this is
# what tells the two apart. Prefer it for monitoring: a monitor attached to the
# bootloader sits there silently forever and looks like a dead sketch.
# The port of the board named by BOARD, and only if it is RUNNING A SKETCH
# rather than sitting in SAM-BA. Both halves matter: the serial picks the
# board, ID_SERIAL_SHORT proves it is not in the bootloader (SAM-BA does not
# report the chip UID, so the property is absent there).
#
# THIS USED TO GLOB EVERY Pro RF AND RETURN THE FIRST ONE RUNNING A SKETCH,
# ignoring PRORF_SERIAL. With two boards attached that is not a search, it is
# an alphabetical accident: 21A8043C sorts before B56628F8, so `make
# monitor-base` opened the SENSOR, and so did every other monitor. find_port
# was already taught to refuse to guess between two boards; this function was
# not, and nothing connected them.
find_app_port() {
    local p
    for p in /dev/serial/by-id/*SAMD21_Pro_RF*"${PRORF_SERIAL}"*; do
        [[ -e "$p" ]] || continue
        if udevadm info -q property -n "$p" 2>/dev/null | grep -q '^ID_SERIAL_SHORT='; then
            printf '%s\n' "$p"; return 0
        fi
    done
    return 1
}

# After an upload the board reboots and USB re-enumerates, which takes a second
# or two and is not a fixed interval. Polling for the port beats sleeping for a
# guess: too short and the monitor opens nothing, too long and you miss the
# banner the sketch printed while waiting for a host.
wait_for_app_port() {
    local timeout="${1:-15}" port
    local deadline=$(( SECONDS + timeout ))
    while (( SECONDS < deadline )); do
        if port="$(find_app_port)"; then printf '%s\n' "$port"; return 0; fi
        sleep 0.1
    done
    return 1
}

require_port() {
    local port
    port="$(find_port)" || fail "cannot pick a board unambiguously (BOARD=$BOARD).
  Attached:
$(list_boards)
  Wanted serial: $PRORF_SERIAL
  Use BOARD=sensor or BOARD=base, or set PRORF_SERIAL directly.
  If nothing is listed: no Pro RF found.
  Check the cable is a DATA cable, not charge-only, then:
      ls -l /dev/serial/by-id/
      lsusb | grep 1b4f"
    printf '%s\n' "$port"
}

# Names what currently holds the port, if anything. Two processes on one tty is
# not blocked by Linux and produces confusing partial reads.
port_users() {
    local port="$1"
    fuser -v "$(readlink -f "$port")" 2>&1 | tail -n +2 || true
}

# Advisory lock, so this project's own scripts do not collide with each other.
# It does not bind anything else -- a stray miniterm can still break a read.
LOCK_FD=
lock_port() {
    local port="$1"
    local lockfile
    lockfile="/tmp/prorf-$(basename "$(readlink -f "$port")").lock"
    exec {LOCK_FD}>"$lockfile" || fail "cannot create $lockfile"
    flock -n "$LOCK_FD" || fail "another script in this project holds $port.
  $(port_users "$port")"
}

# ------------------------------------------------------------- transcripts --
# Claude Code stores each session as JSONL under
# ~/.claude/projects/<encoded-cwd>/. The encoding replaces every character that
# is not alphanumeric or a hyphen with a hyphen, so an absolute path becomes one
# long directory name.
SCRIPTS_DIR="${SCRIPTS_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
PROJECT_DIR="${PROJECT_DIR:-$(cd "${SCRIPTS_DIR}/.." && pwd)}"

encoded_project_dir() {
    printf '%s' "$PROJECT_DIR" | sed 's/[^a-zA-Z0-9-]/-/g'
}

# `die` is `fail` under the name the ported scripts use. Kept as an alias
# rather than renamed through them, so a future diff against the sibling
# project still lines up.
die() { fail "$@"; }

# -------------------------------------------------------------------- misc --
need() { command -v "$1" >/dev/null 2>&1 || fail "$1 is not installed"; }

repo_root() { cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd; }
