# SAMD21-LoRa-ProRF

[![check](https://github.com/JeffreyPeacock/SAMD21-LoRa-ProRF/actions/workflows/ci.yml/badge.svg)](https://github.com/JeffreyPeacock/SAMD21-LoRa-ProRF/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

Firmware for a **SparkFun Pro RF - LoRa, 915MHz (SAMD21)** carrying a chain of
Qwiic sensors: read them, log them to a microSD card, and transmit them over
LoRa to a second Pro RF acting as a base station.

The board is invoice SKU **DD-15178**, the March 2019 black-PCB run of the
design SparkFun now sells as WRL-15836.

## What it is

| | |
|---|---|
| MCU | ATSAMD21G18A, Cortex-M0+, 48 MHz, 256 KB flash, 32 KB SRAM |
| Radio | Hope RFM95W — Semtech SX1276, +20 dBm build, PA_BOOST only |
| Link | 915.0 MHz, SF7 / BW125 / CR4-5, sync word `0x12`, 14 dBm |
| USB | native (the SAMD21 itself), `1b4f:214f` |
| Antenna | Pycom 915 MHz half-wave kit, RP-SMA + U.FL pigtail |

### On the Qwiic chain

| Device | SKU | Address |
|---|---|---|
| BME280 — temperature, humidity, pressure | SEN-14348 | `0x77` |
| CCS811 — eCO2, TVOC | SEN-14348 | `0x5B` |
| Qwiic OpenLog — microSD | DEV-15164 | `0x2A` |

The BME280 and CCS811 are **one breakout board with two I2C addresses**, which
is worth knowing the first time a bus scan reports one more device than there
are boards on the desk.

## Bill of materials

Everything needed to build one end of the link. Prices and stock checked
against sparkfun.com on **2026-08-27** and will drift — see the SKU warning
below, which is not a formality on this board.

| SKU | Part | Notes |
|---|---|---|
| `WRL-15836` | SparkFun Pro RF — LoRa 915 MHz (SAMD21) | the current revision of this design |
| `SEN-14348` | Environmental Combo Breakout — CCS811/BME280 | two devices, one board |
| `DEV-15164` | Qwiic OpenLog | microSD logging over I2C |
| `KIT-15081` | Qwiic Cable Kit — $12.95 | ten cables, 50–500 mm. Cheaper than buying two |
| `PRT-17259` | Flexible Qwiic Cable, 100 mm — $1.95 | if you would rather buy singles |
| `WRL-14676` | Pycom LoRa antenna kit, 915 MHz | RP-SMA half-wave plus a U.FL pigtail |
| `WRL-09145` | Interface cable, SMA to U.FL | **plain SMA, not RP-SMA** — see `docs/antenna.md` |
| — | microSD card | any small FAT32 card; the OpenLog is FAT 8.3 short names only |
| — | 1S LiPo, JST-PH | ≥500 mAh, because the on-board MCP73831 charges at 500 mA |

**Two SKUs here are already retired**, which is the point of dating this table:
`PRT-14427` (the original 100 mm Qwiic cable) is superseded by `PRT-17259`, and
`PRT-17260` (50 mm) is gone. The board itself has three SKUs naming one design
— `WRL-14916`, `DD-15178` and `WRL-15836` — and **a URL that resolves is not a
URL that describes your board**. `CLAUDE.md` explains how that trap works.

You need **two** Pro RF boards to reproduce the link: one runs the sensor
application, one runs the base station.

## Current state

**The link runs end to end.** The sensor board reads its two environmental
sensors, writes each record to microSD, and transmits it. The base board
receives, stamps it, echoes it to the serial port and logs it. Measured on the
bench: **0.0% loss at −49 to −50 dBm**.

### The two roles, and why they keep time differently

| | sensor board | base station |
|---|---|---|
| Job | read, log, transmit | receive, stamp, echo, log |
| Host attached | no | yes, assumed permanently |
| Timestamps | **uptime**, `+44.9` | **real UTC**, `05:57:01` |

This asymmetry is deliberate. The Pro RF has no RTC crystal and no backup cell,
so a board with nothing attached cannot know the time — and a stamp that
*looked* like a date would be a fiction. The base station is the end with a
host, so it is the end that can hold a real clock, and it tags every record
with the time **it** received it.

**The sequence number is what ties the two together.** One record, one number,
correlatable across both logs regardless of what either end calls "now".

At startup the base asks for a clock and waits up to **two minutes**, skippable
with any key. It keeps receiving throughout that wait — a base that went deaf
while asking about its clock would be a bad trade. `scripts/base-station.py`
is the other half of this: it finds the board, sets the clock, and parses what
comes back.

There are three Pro RFs and they share one USB VID:PID, so only the chip UID
distinguishes them — see `CLAUDE.md`.

| Board | UID prefix | Port, both attached | Holds |
|---|---|---|---|
| sensor | `21A8043C` | `/dev/ttyACM0` | the sensor application |
| base | `B56628F8` | `/dev/ttyACM1` | the base application |
| dead | — | never enumerates | nothing; `-71` at enumeration |

Those ports are the normal arrangement, not a guarantee. `ttyACMn` is assigned
in plug order, so a board brought up alone takes `ttyACM0` whichever one it is.
Never select a board by tty — `BOARD=sensor` and `BOARD=base` resolve the UID
through `/dev/serial/by-id/`.

One thing is not working:

- **The base board's OpenLog** — dead, not wedged, and not the cable. The
  evidence is in `CLAUDE.md`. The confirming test is to move it to the sensor
  board's chain, where the same model demonstrably works.

## Environment

PlatformIO builds; a pyenv virtualenv named `LoRa` supplies Python and
`pyserial`. They are deliberately separate — PlatformIO manages toolchains
shared with the sibling projects and lives outside any virtualenv.

```bash
pyenv virtualenv 3.12.12 LoRa     # skip if it exists
pyenv local LoRa                  # writes .python-version
pip install --upgrade pip pyserial esptool
pio pkg install -g -p platformio/atmelsam
pio pkg install -g -t platformio/tool-bossac    # the SAMD21 uploader
```

Verify with `./scripts/board-check.sh`.

`esptool` is in that virtualenv for the *ESP32* project next door. **It is not
a tool for this board** — the SAMD21 equivalent is `bossac`, which PlatformIO
invokes for you.

## Building and uploading

```bash
make                        # what you can do
make build                  # every program
make radio_rx               # one program
make flash APP=probe        # build and upload
make run APP=probe          # ...then open a serial monitor
make test                   # host-side unit tests, no board needed
make check                  # build everything + tests + shellcheck
```

`make` is a thin front door over `pio`; `pio run -e sensor -t upload` works
just as well.

| Program | What it does |
|---|---|
| `probe` | radio `RegVersion` + a real RST test + Qwiic scan |
| `i2c_scan` | the bus scan alone — no radio, no libraries |
| `radio_rx` | listen, decode our frames, report RSSI/SNR |
| `radio_tx` | beacon numbered packets at 5 dBm for bench work |
| `qwiic_read` | every sensor, printed; no radio, no logging |
| `base` | LoRa → serial + SD, ACK with sequence state |
| `sensor` | the full application: sensors → SD → LoRa |

`sensor` and `radio_tx` transmit; everything else receives only.

`probe` and `base` receive only and are safe with nothing attached. **`sensor`
transmits**, and transmitting into an open U.FL connector can damage the PA on
an RFM95W. See [`docs/antenna.md`](docs/antenna.md).

Run `probe` first, after any wiring change, and before believing any negative
result.

### If an upload fails

The SAMD21 has native USB and PlatformIO flashes it by opening the port at
1200 baud, which the running sketch interprets as "reset into the bootloader".
A sketch that has hung cannot answer that.

> **Double-tap RESET.** The LED pulses slowly and the board waits indefinitely
> for `bossac`. Upload again.

The USB PID stays `1b4f:214f` in the bootloader — it is the manufacturer string
(`Arduino LLC`) and the missing USB serial number that change. See
[`docs/hardware.md`](docs/hardware.md).

## Talking to it

```bash
./scripts/board-check.sh   # host, USB, port, permissions, and what it prints
./scripts/monitor.sh       # serial monitor; Ctrl-C exits
./scripts/base-station.py  # set the base's clock, then monitor and parse
```

`board-check.sh` separates the cases that all look like "the board is dead": a
charge-only cable, a port held by another process, a missing `dialout` group,
and a board already sitting in its bootloader.

Use the by-id path, not `/dev/ttyACM0` — the SAMD21 exposes its chip UID as a
USB serial number, so udev names it uniquely, while `ttyACM0` moves whenever
something else is plugged in. The scripts resolve it themselves.

## What comes out

Every line either firmware emits is a checksummed NMEA-style sentence, so a log
parses with `split(',')` and no state:

```
$PJPT,<seq>,<ms>,<tempC>,<rh>,<hPa>,<altM>,<co2>,<tvoc>*HH
<seq>:$PJPR,<seq>,<received>,<expected>,<missing>,<rssi>,<snr>*HH
$PJPA,<seq>,<highest>,<received>,<upRssi>,<upSnr>,<downRssi>*HH
```

Every line the base prints has the same shape — `<kind>:<time> <seq>:<sentence>`
— so one rule strips the prefix off all of them. **The checksum covers the
sentence only, never the sequence prefix.**

The base also prints a **field legend** at startup, on every serial attach, and
with each health report. Every legend line begins with `#`, so an automated
reader drops the lot with `grep -v '^#'`. It goes to the serial port only and
never to the SD card, because `BASE.LOG` has to stay byte-comparable with
`SENSOR.LOG`.

`$P` is NMEA 0183's reserved proprietary prefix and `JP` is this project's
manufacturer code, so these cannot collide with a standard NMEA sentence.

**An absent field is empty, not zero.** `,,` means no reading; `,0.00,` means
measured zero.

The record the sensor board transmits is byte-identical to the line it writes to its own
SD card, so `SENSOR.LOG` and `BASE.LOG` diff directly — every line in one and not
the other is a packet lost on the air.

Full format in [`docs/link-protocol.md`](docs/link-protocol.md).

## Design

Three decisions carried through everything:

**The ACK is a status report, not a gate.** The sensor board transmits and carries on.
The 2019 code blocked for up to two seconds after every packet waiting for the
base — and out of range is the normal condition for a node that gets carried
away and brought back. Worse, blocking can lose data outright: a device that
streams into an internal buffer discards the overflow when nobody reads it, and
reports no error when it does.

**The SD card is the record of record.** Logging never depends on the radio and
the radio never waits for the log. The card is written before the packet is
sent.

**Every device is optional.** The 2019 sketches called `while(1)` on any failed
`begin()`, which on a shared Qwiic bus meant one loose cable stopped the sensors
that still worked, stopped the logging, and made the board unflashable. Nothing
here freezes in `setup()`.

## Layout

```
CLAUDE.md                   facts that were expensive to establish
Makefile                    make help -- the CLI front door
platformio.ini              one environment per program; pinned library versions

docs/hardware.md            identification, pin map, and which SKU this is
docs/programming.md         toolchain, the 1200-baud touch, and how uploads fail
docs/project-layout.md      why the tree is shaped this way; CLion notes
docs/hardware.md            the board itself: pins, power, LEDs, USB identities
docs/programming.md         uploading, the 1200-baud touch, recovering a hung board
docs/project-layout.md      why src/apps and src/tools are split this way
docs/link-protocol.md       radio settings, frame layout, time-on-air
docs/radio-settings-reply.md getting a message across to the ESP32 gateway
docs/qwiic-bus.md           the chain, rate limits, and pinned library versions
docs/qwiic-timing.md        measured bus timing, and the OpenLog's 400 kHz limit
docs/receiving-elsewhere.md decoding this link on other hardware
docs/antenna.md             the Pycom kit, the U.FL jumper, bench alternatives

lib/                        OUR shared libraries -- committed
  board/board.h             THE pin map -- one definition
  nmea/nmea.h               integer float formatting and NMEA checksums
  telemetry/telemetry.h     the wire format
  rfm95/rfm95.{h,cpp}       RFM95W bring-up: reset, init, frequency, power
  datalog/datalog.{h,cpp}   Qwiic OpenLog, made optional
  qwiic/qwiic.{h,cpp}       the Qwiic sensor chain behind one interface

src/apps/sensor/              sensors -> SD -> LoRa
src/apps/base/              LoRa -> serial + SD, ACK with sequence state
src/tools/probe/            radio RegVersion + RST test + I2C scan
src/tools/i2c_scan/         Qwiic bus scan alone, no radio, no libraries
src/tools/radio_rx/         listen, decode, report RSSI/SNR
src/tools/radio_tx/         beacon numbered packets, low power
src/tools/qwiic_read/       read every sensor, no radio, no logging

test/test_nmea/             host-side: formatting and checksums
test/test_telemetry/        host-side: frame layout

scripts/board-check.sh      verify host, USB, port and board end to end
scripts/capture.sh          serial capture that survives a reset
scripts/dump-flash.sh       read the firmware off the board with bossac
scripts/flash.sh            build and upload one program
scripts/monitor.sh          serial monitor on whichever ttyACM it landed on
scripts/export-transcript.sh render a session log to text, with secrets masked
scripts/lib/mask.py         the one definition of what a secret looks like here
scripts/lib/common.sh       port autodetect, port locking, output helpers

.claude/commands/           five slash commands, adapted from the sibling project
```

## Source links

- Product page (v1.1, WRL-15836) — <https://www.sparkfun.com/products/15836>
- Retired v1.0 listing — <https://www.sparkfun.com/products/retired/14916> — **serves v1.1 content**
- Hookup guide — <https://learn.sparkfun.com/tutorials/sparkfun-samd21-pro-rf-hookup-guide>
- Hardware repo — <https://github.com/sparkfun/SAMD21_Pro_RF/tree/v11>
- Schematic (v1.1) — <https://cdn.sparkfun.com/assets/f/f/8/e/f/SAMD21_Pro_RF_v11_Schematic.pdf>
- SparkFun's paired LoRa examples — <https://github.com/sparkfun/SparkFun_SAMD21_Pro_RF_Arduino_Examples>
- SAMD21 datasheet — <https://cdn.sparkfun.com/assets/6/3/d/d/2/Atmel-42181-SAM-D21_Datasheet.pdf>
- SX1276 datasheet — <https://cdn.sparkfun.com/assets/7/7/3/2/2/SX1276_Datasheet.pdf>
- RadioHead — <https://www.airspayce.com/mikem/arduino/RadioHead/>

## License

[MIT](LICENSE) — Copyright (c) 2026 Jeffrey Peacock
