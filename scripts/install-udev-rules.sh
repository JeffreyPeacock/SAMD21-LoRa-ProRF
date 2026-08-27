#!/usr/bin/env bash
# Silence "Warning! Please install 99-platformio-udev.rules" -- and get the
# side effects that warning is actually about.
#
# Needs root, so run it yourself: this is the one thing here that cannot be
# done from an agent session.
#
#     ./scripts/install-udev-rules.sh
#
# What the rules do, beyond removing the message:
#   - tag known development boards so udev applies consistent permissions
#     rather than leaving them to the default tty group ownership
#   - stop ModemManager grabbing a board the moment it enumerates. That is the
#     one with teeth: ModemManager probes new ACM devices by writing AT
#     commands at them, which lands in the middle of a bossac upload and
#     corrupts it. The failure looks random and is not.
#
# PlatformIO's file ships with PlatformIO itself, so this installs the version
# matching the installed core rather than whatever is on the internet today.
#
# It installs TWO files, because PlatformIO's does not cover this board.
# Checked 2026-08-21 against PlatformIO 6.1.19: its rules contain no entry for
# vendor 1b4f, so a Pro RF still enumerates with ID_MM_CANDIDATE=1 and is still
# probed. etc/udev/99-prorf-udev.rules fixes that.

set -euo pipefail
# shellcheck source=scripts/lib/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/lib/common.sh"

DEST=/etc/udev/rules.d/99-platformio-udev.rules
PRORF_DEST=/etc/udev/rules.d/99-prorf-udev.rules
PRORF_SRC="$(repo_root)/etc/udev/99-prorf-udev.rules"

SRC="$(find "$HOME/.platformio" -name '99-platformio-udev.rules' 2>/dev/null | head -1)"
[[ -n "$SRC" ]] || fail "no 99-platformio-udev.rules found under ~/.platformio.
  Fetch it instead:
    curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core/develop/platformio/assets/system/99-platformio-udev.rules"

step "source"
ok "$SRC  ($(wc -l < "$SRC") lines)"

if [[ -f "$DEST" ]]; then
    if cmp -s "$SRC" "$DEST"; then
        ok "$DEST is already installed and identical -- nothing to do"
        exit 0
    fi
    warn "$DEST exists and differs; it will be replaced"
    diff -u "$DEST" "$SRC" | head -20 || true
fi

step "installing to $DEST   (needs sudo)"
sudo install -m 0644 "$SRC" "$DEST"
ok "installed"

step "reloading udev"
sudo udevadm control --reload-rules
sudo udevadm trigger
ok "reloaded"

step "installing $PRORF_DEST   (needs sudo)"
sudo install -m 0644 "$PRORF_SRC" "$PRORF_DEST"
ok "installed -- covers vendor 1b4f, which PlatformIO's rules do not"

step "reloading udev again"
sudo udevadm control --reload-rules
sudo udevadm trigger
ok "reloaded"

step "group membership"
for g in dialout plugdev; do
    if id -nG | tr ' ' '\n' | grep -qx "$g"; then
        ok "in $g"
    else
        warn "NOT in $g -- run: sudo usermod -aG $g $(id -un)   then log out and back in"
    fi
done

step "verifying"
if [[ -e /dev/ttyACM0 ]]; then
    if udevadm info -q property -n /dev/ttyACM0 2>/dev/null | grep -q 'ID_MM_DEVICE_IGNORE=1'; then
        ok "ModemManager is now told to ignore the board"
    else
        warn "ID_MM_DEVICE_IGNORE is not set yet -- unplug and replug the board"
    fi
fi

echo
dim "Unplug and replug the board for the new rules to apply to it."
dim "Then confirm with:"
dim "  udevadm info -q property -n /dev/ttyACM0 | grep ID_MM"
