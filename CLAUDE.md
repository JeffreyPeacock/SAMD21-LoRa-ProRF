# Project instructions

Working notes for this repository. Facts that were expensive to establish and
are easy to get wrong from memory or from generic documentation.

## What this project is

Firmware for a **SparkFun Pro RF - LoRa, 915MHz (SAMD21)** — a SAMD21G18A with
an RFM95W (SX1276) and a Qwiic connector. It carries four Qwiic sensors, logs
to a Qwiic OpenLog, and transmits to a second Pro RF acting as a base station.

It replaces a set of 2019 Arduino IDE sketches under
`~/Workspace/JeffreyPeacock.com/Embedded/Arduino/`. Those still exist and are
the specification for what the project was trying to do.

**The attached battery is a 2019 LiPo.** Markings: `PL 115599` /
`3.7V 5000mAh` / `2019 01 25` — a 1S 5 Ah cell. It still runs the board for
hours, so its capacity is not zero, but a cell that old has lost capacity and
gained internal resistance, and the failure mode of an aged LiPo is venting.
Bench use where someone can see it is fine; leave it charging unattended and
you are taking a risk for no reason. The board charges it over USB at 500 mA
through an MCP73831 — see `docs/hardware.md`.

The sibling project `../ESP32-LoRa-1Ch-Gateway` is a SparkFun 1-channel gateway
with the same radio silicon. **Do not copy its hardware facts here** — see
below. Read its `CLAUDE.md` for context on that board.

## The board's SKU is DD-15178, and no product page describes it

A SparkFun invoice dated **2019-03-06** lists
`DD-15178  SparkFun Pro RF - LoRa, 915MHz (SAMD21, Black Board)`, $29.95. A
special run, not a catalogue SKU.

Three SKUs name this design: `WRL-14916` (v1.0, retired), `DD-15178` (this
board), `WRL-15836` (v1.1, current). <https://www.sparkfun.com/products/15836>
is the live page for the *design* and is **not** evidence about this unit.

This is the same trap the ESP32 gateway project documents from the other side:
SparkFun migrated commerce platforms and old SKUs redirect to the current
revision. There, `products/15006` serves the WRL-18074 page and gets the flash
size wrong. Here, `products/retired/14916` serves WRL-15836 content.
**A URL that resolves is not a URL that describes your board.**

Unconfirmed for this unit: whether it has the v1.1 VDDCORE fix or the SWD pads.
Read them off the PCB before relying on either.

## Hardware facts

| | |
|---|---|
| MCU | ATSAMD21G18A, Cortex-M0+, 48 MHz, 256 KB flash (8 KB bootloader), 32 KB SRAM |
| Radio | Hope RFM95W = Semtech **SX1276**, the +20 dBm build — **PA_BOOST only** |
| USB | **native** — the SAMD21 is the USB device, no bridge chip |
| USB ID | `1b4f:214f` — **unchanged in the bootloader**, see below |
| Serial | `21A8043C5150484735202020FF09181F` — the chip's own 128-bit UID |
| Port | `/dev/ttyACM0`, stable path under `/dev/serial/by-id/` |

Radio pin map, from SparkFun's hookup guide, and the one definition is
`lib/board/board.h`:

```c
CS/NSS 12   DIO0 6   RST 7   DIO1 10   DIO2 11   LED 13
```

**SPI is not a trap on this board** — the radio is on the pins the SAMD21
Arduino core already uses, so plain `SPI` is correct. What *is* easy to miss:
SparkFun's headline example `RH_RF95 rf95(12, 6)` names only two of five pins,
and RadioHead never touches RST, so a sketch written from that line cannot put
the radio in a known state. Both firmwares pulse pin 7 before `init()`, and
`src/tools/probe` measures whether that pulse actually resets the part rather than
trusting the tutorial.

**Nothing from the ESP32 gateway's pin map applies here.** Different MCU,
different bus, different pins. Its radio is at NSS 16 / SCK 14 / MISO 12 /
MOSI 13 / DIO0 26 on the HSPI-side pins, and its reset line does not work at
all. The only thing the two boards share is the SX1276.

## The 1200-baud touch, and the one way uploads fail

Native USB means opening the port **at exactly 1200 baud is a command**: the
Arduino SAMD core resets into the SAM-BA bootloader. That is how
`pio -t upload` flashes without a button press.

It follows that **the touch is handled by the running sketch, so a hung sketch
cannot answer it.** The 2019 `AllSensors.ino` calls `while(1);` on any failed
sensor `begin()` — one unplugged Qwiic cable and the board is unflashable over
USB.

Recovery: **double-tap RESET.** Two presses inside ~half a second, the board
shows a slowly pulsing LED and waits indefinitely.

**The bootloader does NOT change the PID**, contradicting the PlatformIO board
definition's `0x215f` hwid, which has never been seen here. Both states report
`1b4f:214f`. What changes: manufacturer `SparkFun` -> `Arduino LLC`, and the
USB serial number (the chip UID) **disappears**, because SAM-BA does not read
it. It also usually returns on the same `/dev/ttyACMn`.

So neither the PID nor a new tty detects it — the first `dump-flash.sh` waited
for a new tty and timed out on a board already in the bootloader. **Test for
the absence of `ID_SERIAL_SHORT`:**

```bash
udevadm info -q property -n /dev/ttyACM0 | grep -E 'ID_VENDOR=|ID_SERIAL_SHORT='
```

**No firmware in this project calls `while(1)` in `setup()`.** Every device is
optional; a missing one is recorded and the sensor board keeps running.

Baud rate is otherwise fiction on this board — the CDC moves bytes at USB speed
and the number is never consulted. The 2019 sketches used 9600 in one file and
115200 in another and both worked. Do not spend time on it.

## esptool is not a tool for this board

It is in the same pyenv (`LoRa`) because that virtualenv also serves the ESP32
gateway project. The SAMD21 is not an Espressif part; the equivalent is
`bossac`, which PlatformIO invokes. Reaching for `esptool` when a Pro RF
misbehaves is a dead end.

PlatformIO lives at `~/.local/bin/pio` with packages in `~/.platformio`,
deliberately **outside** any virtualenv and shared with the sibling projects.

## Three Pro RFs: one sensor, one base, one dead

They are the same model with the same `1b4f:214f`, so **only the chip UID in
the USB serial number tells them apart.** `scripts/lib/common.sh` holds both:

| Role | UID | Port, as of 2026-08-24 |
|---|---|---|
| sensor | `21A8043C5150484735202020FF09181F` | `/dev/ttyACM0` |
| base | `B56628F85150484735202020FF082C1A` | `/dev/ttyACM1` |
| dead | none — it never enumerates, so it has no `by-id` entry | — |

Select one with `BOARD=sensor` (the default) or `BOARD=base`. **With two boards
attached, `find_port` refuses to guess rather than picking one.** It had to:
`flash.sh` stopped passing `--upload-port` so PlatformIO could rediscover the
board after the 1200-baud touch, which is correct with one board and a coin
flip with two — and it flashed the wrong one.

**The link is proven at both ends**: 57 received, 57 expected, 0 missing,
RSSI -48 dBm, SNR 9–10 dB at about a metre. So `pio run -e base` has hardware
to run on, and the ESP32 gateway is no longer the only receiver. It remains a
useful fallback, and its SX1276 driver already matches this link on four of the
five parameters that matter — sync word `0x12`, SF7, BW125, CR4/5 — with only
the frequency differing (903.9 vs 915.0 MHz). To decode there: skip 4 bytes of
RadioHead header and 8 bytes of `telemetry::Header`; the rest is the ASCII
telemetry sentence. That is *why* the record is ASCII rather than packed
binary — it costs airtime and buys decodability by anything.

**The third board was confirmed dead on 2026-08-23**, and the diagnosis is in
`docs/hardware.md`. In short: `device descriptor read/64, error -71` repeating
until `unable to enumerate`, with the *same cable* that works on the sensor
board, and **no LED response to a double-tap of RESET**. The part is powered
and attaches, but is not executing — the bootloader cannot help, because it
needs the same USB clock and data lines. Do not re-diagnose this; it is not a
cable, not the firmware, and not the VDDCORE defect.

## Historical: the sensor board as found, 2026-08-21

Kept because it is the baseline the recovery work started from, **not a
description of the board now** — it has run this project's firmware since
2026-08-22.

As found it enumerated as `1b4f:214f` and **printed nothing** — 12 s of
listening at 9600 and 115200 with DTR toggled produced zero bytes. It was
running something that either never printed or hung before its first output.
Given `AllSensors.ino` freezes on a failed sensor `begin()`, the second was
likely. **That was never evidence of a fault**, and it was not one.

## A dead I2C device and a wedged one look identical from the MCU

Established while diagnosing the base station's OpenLog, which **is dead**. It
shows pull-ups; `qwiic_health` got no address answer at 400, 100, 50 or 10 kHz
in either pin orientation.

**It is not a clean "never answers", though.** On the first boot after the base
app was flashed on 2026-08-25 it **did ACK `0x2A`** — `Wire.endTransmission()`
returned 0 inside `datalog::begin()`'s retry loop, at 50 kHz under `SlowBus` —
and then returned `0xFF` to `getVersion()`, printing `version 255.255`. So the
part answers its address intermittently and runs nothing behind it. With the
LEDs dark that still reads as dead rather than wedged, but **do not repeat the
claim that it never ACKs** — it has, once, and a later session testing that
absolute would find it false.

**Seeing the pull-ups rise proves all four conductors reach the module.** A
Qwiic device's pull-ups sit between the bus lines and the 3V3 the *cable*
delivers, so a rise needs 3V3 and GND to bias the resistors and SDA and SCL to
carry them. That one measurement rules out all four broken-conductor cases; the
bit-banged scan in both orientations rules out a swapped pair. So it is not the
cable, and `qwiic_health` says so.

What it cannot tell you is dead from wedged, because neither answers. **The
power LED settles it, and it beats any measurement made from the MCU** — dark
while the pull-ups rise means power arrives and the module does nothing with
it. The first verdict claimed "wedged, power-cycle it"; the repository owner
power-cycled and it did not help, which retired that theory.

Two measurement traps found here, both of which produce confident wrong
answers:

- **A floating CMOS input reads HIGH.** A presence test that only reads a line
  reports a healthy bus with nothing plugged in. `qwiic_health` now actively
  drives low and releases.
- **An unpowered device's two pull-ups meet at its dead 3V3 rail**, so the
  MCU's internal pull-up on one line can raise the other *through* them.
  `riseTimeUs()` now floats the other pin before measuring. That was not the
  explanation here, but the measurement was unsound either way.

## Open: the SD write throughput gap, 25x and unexplained

`openlog_test` measures **3846 B/s** writing to the OpenLog on its own. The
same device in the sensor application manages **151 B/s**. Nothing found so far
accounts for the factor of 25, and it is not the 400 kHz fault, which was
separately fixed by dropping to 50 kHz per transaction.

**Batching was tried and is a negative result. Do not re-run it.** Accumulating
records and writing less often trades a **0.283 s** blocking write for a
**2.6 s** one, worst case 3.41 s. Total throughput barely moves, because the
cost is per byte rather than per write, and the blocking window grows by an
order of magnitude.

That window is the thing to protect. Nothing else on the chain can be serviced
while an SD write blocks, so a longer one is worse in every way that matters
even when the byte count is identical.


## Frequency: 915.0 MHz, read off the board

The 2019 sketches disagreed. `BaseStation.ino` set 915.0, the other two set
921.2, and **all three printed banners naming a different number again** —
`"RFM Client!: 903.9"` sits directly above `float frequency = 921.2;`.

**The tie was broken by the firmware dump**, not by picking a favourite: the
image holds the float `915.0` at flash offset `0xf588`, in the initialised-data
region where a `float frequency = 915;` global lives. The only other
frequency-shaped constant is `434.0` inside `RH_RF95::init()` — RadioHead's
default. So the last working pair was `BaseStation.ino` + this node on 915.0,
and the 921.2 files are the abandoned branch.

Two radios on different frequencies hear nothing and report no error, and the
banners made it worse by looking like confirmation. One constant now, in
`board.h`, and both firmwares print what they actually set.

## Library versions are pinned, and that is not optional

These are 2019 breakout boards whose libraries have had breaking changes since.
The 2019 sketch's `CCS811Core::status` / `SENSOR_SUCCESS` are now
`CCS811Core::CCS811_Status_e` / `CCS811_Stat_SUCCESS`, and the status-returning
entry point is `beginWithStatus()`. An unpinned build breaks on somebody else's
release schedule, in a way that reads as a hardware fault.

**Exact tag strings matter too.** SparkFun has changed tag conventions partway
through a repository's life — `V_1.0.1` in one place, `v1.0.2` in another — and
only the exact string resolves. A near miss fails with "Remote branch not
found", which reads like the repository is gone rather than the tag.

**And a macro from one library can change a class layout in another.** Two
SparkFun libraries defining the same unguarded `#define`, where one of them
uses it to size a member array, is an ODR violation the linker cannot see: it
corrupts memory rather than failing to build, and the only symptom is a
`warning: "..." redefined` that looks cosmetic. If a build ever emits one of
those, treat it as an error and fix the include order with `#undef` and a
`static_assert` that makes a future reordering fail loudly.

## The two boards keep time differently, on purpose

| | sensor board | base station |
|---|---|---|
| Host attached | no | yes, assumed permanently |
| Timestamps | **uptime**, `+44.9` | **real UTC**, `05:57:01` |

The Pro RF has no RTC crystal and no backup cell, so a board with nothing
attached cannot know the time. **A stamp that looked like a date would be a
fiction, and a fiction in a log is worse than an honest counter.** So the
sensor board stamps uptime and does not pretend otherwise; `lib/rtc` is not
even linked into it.

The base station is the end with a host, so it is the end that can hold a real
clock, and it tags every record with the time **it** received. `make clock`
sets it, and at startup the base asks for one and waits 30 s.

**The sequence number is what ties the two together** — one record, one number,
correlatable across both logs regardless of what either end calls "now".

Two things that bit while building the startup wait, both of which made the
feature look absent rather than broken:

- **Opening the port toggles DTR and can leave a stray byte in the buffer**, so
  an "any key skips" check written as a bare `SerialUSB.available()` is true
  before anyone has pressed anything. It skipped the wait every single time.
  Drain the buffer before starting the wait.
- **A wait that does not service the radio throws packets away.** Thirty
  seconds is about ninety records here. The wait loop receives and stamps
  normally throughout; records arriving during it simply carry uptime.

## Design decisions to preserve

- **The ACK is a status report, not a gate.** The sensor board never waits for the
  base. A missing ACK costs nothing. This is the change the repository owner
  had already started: allow missed packets, let sequence numbers show the gaps.
- **The SD card is the record of record.** Logging never depends on the radio,
  and the radio never waits for the log. The sensor board writes to the card *before*
  transmitting.
- **Every Qwiic device is optional.** A `Reading` with every `have*` false is a
  valid result.
- **An absent field is empty, not zero.** `,,` means no reading; `,0.00,` means
  measured zero. The 2019 records could not tell those apart.
- **Node and base logs are byte-comparable.** The transmitted record is
  identical to the line the sensor board wrote to its own card, so the two logs diff
  directly and every missing line is a lost packet.

## Stay on this board's port

**This project owns `/dev/ttyACM0` — the Pro RF — and nothing else.**

`/dev/ttyUSB1` is the ESP32 gateway in `../ESP32-LoRa-1Ch-Gateway`, and it is
**managed by another developer**. Do not open it, monitor it, capture from it,
flash it, or run that project's scripts. Not read-only either: opening a tty
is not a passive act — it asserts DTR, can reset the far board, and two
processes on one serial port produce corrupt transfers.

`/dev/ttyUSB0` is a CP2102 belonging to a third project. Same rule.

When the far end needs checking, **say what to look for and let its owner look**.
`docs/radio-settings-reply.md` exists for exactly that: it is the message to
hand over, not a licence to go and verify it.

## Repository conventions

**There is no git repository and no remote yet.** If one is created, commit
locally; do not report a push that did not happen.

**Do not add Claude attribution to commits** — no `Co-Authored-By` trailer, no
"Generated with" line. Standing instruction from the repository owner, carried
over from the sibling project.

**Third-party libraries are not committed.** `platformio.ini` pins them and
PlatformIO fetches them into `.pio/libdeps/`.
