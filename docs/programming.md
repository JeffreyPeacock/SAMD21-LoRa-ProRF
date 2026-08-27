# Building, uploading, and talking to this board

## The environment

PlatformIO does the building; the pyenv virtualenv named `LoRa` supplies Python
and `pyserial`. They are deliberately separate — this matches the sibling ESP32
gateway project, and the reason is that PlatformIO manages a couple of hundred
megabytes of toolchains that every embedded project on this machine shares.

| | Where | Verify |
|---|---|---|
| PlatformIO Core 6.1.19 | `~/.local/bin/pio`, packages in `~/.platformio` | `pio --version` |
| `atmelsam` platform 8.3.0 | `~/.platformio/platforms/atmelsam` | `pio pkg list -g --only-platforms` |
| arm-none-eabi-gcc 7.2.1 | pulled by the platform | — |
| **bossac 1.9.1-arduino2** | `~/.platformio/packages/tool-bossac/bossac` | `bossac --help` |
| Python 3.12.12 + pyserial 3.5 | pyenv `LoRa`, pinned by `.python-version` | `python -c "import serial"` |
| esptool 5.3.1 | pyenv `LoRa` | `esptool version` |

**esptool is irrelevant to this board.** It is an Espressif tool and the SAMD21
is not an Espressif part — nothing here uses it. It is in the same virtualenv
because that virtualenv also serves the ESP32 gateway project. Reaching for
`esptool` when a Pro RF misbehaves is a dead end; the SAMD21 equivalent is
`bossac`, which PlatformIO invokes for you.

To rebuild the environment from nothing:

```bash
pyenv virtualenv 3.12.12 LoRa     # skip if it exists
pyenv local LoRa
pip install --upgrade pip pyserial esptool
pio pkg install -g -p platformio/atmelsam
pio pkg install -g -t platformio/tool-bossac    # the SAMD21 uploader
```

`tool-bossac` is marked optional by the platform, so it is **not** fetched with
`atmelsam` — PlatformIO pulls it lazily on the first upload. Installing it up
front means the first upload is not also a download, which matters when the
board is sitting in its bootloader waiting.

## Building

```bash
pio run              # all three environments
pio run -e probe     # I2C scan + SX1276 RegVersion.  Safe with no antenna.
pio run -e base      # receiver.                      Safe with no antenna.
pio run -e sensor      # sensor node.  TRANSMITS -- attach an antenna first.
```

Current sizes, for reference when something suddenly grows:

| Environment | Flash | RAM |
|---|---|---|
| `probe` | 14.3 KB / 256 KB | 2.8 KB / 32 KB |
| `base` | 36.6 KB | 3.9 KB |
| `sensor` | 57.2 KB | 5.1 KB |

There is a lot of headroom. The SAMD21's 32 KB of SRAM is the tighter of the
two and the radio buffers are the bulk of it: RadioHead keeps a 255-byte
receive buffer per instance, and `proto::Telemetry` is another 251.

## Uploading

```bash
./scripts/flash.sh probe          # build, upload
./scripts/flash.sh base --monitor # ...and open a serial monitor after
```

or directly:

```bash
pio run -e probe -t upload
```

### How the upload actually works, and the one way it fails

The SAMD21 has **native USB** — no CH340, no CP2102. The chip itself is the
USB device, which has two consequences that do not apply to a bridge-chip board:

1. **Baud rate is fiction.** `SerialUSB.begin(9600)` and a host opening at
   115200 both work, because the CDC layer moves bytes at USB speed and the
   rate is a number nobody consults. The 2019 sketches used 9600 in one file
   and 115200 in another and both worked. Do not spend time on it.

2. **Opening the port at exactly 1200 baud is a command, not a setting.** The
   Arduino SAMD core watches for it and resets into the SAM-BA bootloader. That
   "1200-baud touch" is how `pio ... -t upload` puts the board in a flashable
   state without anyone pressing a button, and it is why `upload_protocol =
   sam-ba` and `use_1200bps_touch` appear in the board definition.

The failure mode follows directly: **the touch is handled by the running
sketch's USB stack, so a sketch that has hung cannot answer it.** The 2019
`AllSensors.ino` calls `while(1);` on any failed sensor `begin()` — one
unplugged Qwiic cable and the board becomes unflashable over USB.

The recovery is manual and always works:

> **Double-tap RESET.** Two presses within about half a second. The LED on
> pin 13 pulses slowly and the board sits there indefinitely waiting for
> `bossac`. Upload again.

**Do not expect the USB PID to change.** It stays `1b4f:214f`; what changes is
the manufacturer string (`Arduino LLC`) and the loss of the USB serial number.
See `docs/hardware.md`.

`scripts/flash.sh` prints this when an upload fails, and `scripts/board-check.sh`
says so when it finds the board already in that state.

The firmware in this project never calls `while(1)` in `setup()`. Every device
is optional: a missing one is recorded and the board carries on. That is not
tidiness — a sketch that freezes in `setup()` cannot answer the 1200-baud
touch, so one unplugged Qwiic cable would leave the board unflashable until
someone double-taps RESET.

## Talking to it

```bash
./scripts/board-check.sh    # host, USB, port, permissions, and what it prints
./scripts/monitor.sh        # miniterm, Ctrl-] to exit
```

`board-check.sh` is the thing to run before believing any negative result. It
distinguishes the cases that all look like "the board is dead": a charge-only
cable (nothing enumerates), a port held by another process, a missing `dialout`
group, and a board sitting in its bootloader.

**Find the port by its by-id path, not `/dev/ttyACM0`.** The SAMD21 exposes its
128-bit chip UID as the USB serial number, so udev can name it uniquely:

```
/dev/serial/by-id/usb-SparkFun_SFE_SAMD21_Pro_RF_21A8043C…FF09181F-if00
```

`ttyACM0` is assigned in plug order and moves the moment anything else is
connected. The scripts here resolve the by-id path themselves.

**Two processes on one tty is not blocked by Linux** and produces confusing
partial reads. `scripts/lib/common.sh` provides `lock_port`, which the scripts
take before touching the port; it binds only this project's scripts, so a stray
`miniterm` can still interfere. `fuser -v /dev/ttyACM0` says who has it.

## The board as found, 2026-08-21

Enumerates correctly as `1b4f:214f` and **prints nothing** — twelve seconds of
listening at both 9600 and 115200, with DTR toggled, produced zero bytes. It is
running *something* (the bootloader would report no USB serial number), but either that sketch
never prints or it hung before its first output. Given that `AllSensors.ino`
freezes on a failed sensor `begin()`, the second is the likely explanation.

**This is not evidence of a fault.** Upload `probe` and the board will answer.

## Debugging

The board definition declares `jlink`, `blackmagic` and `atmel-ice` as upload
protocols and carries an SVD, so `pio debug` works with a hardware probe. That
needs SWD pads, which are a v1.1 addition — **look for them on the PCB before
buying a probe**. See `docs/hardware.md`.

Without a probe, the debugging tool is `SerialUSB` and `src/tools/probe`.
