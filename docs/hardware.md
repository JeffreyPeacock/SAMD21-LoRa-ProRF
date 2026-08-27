# Hardware — SparkFun Pro RF - LoRa, 915MHz (SAMD21)

## What this board is

| | |
|---|---|
| MCU | Microchip **ATSAMD21G18A**, Arm Cortex-M0+, 48 MHz |
| Flash | 256 KB (**8 KB reserved for the bootloader** — sketches load at `0x2000`) |
| SRAM | 32 KB |
| Radio | Hope **RFM95W** — Semtech **SX1276** silicon, the +20 dBm build |
| Band | 902–928 MHz ISM (Americas) |
| USB | **Native** — the SAMD21 is the USB device; there is no bridge chip |
| USB ID | `1b4f:214f` — **the same in the bootloader**, see below |
| Serial number | `21A8043C5150484735202020FF09181F` — the SAMD21's own 128-bit chip UID |
| Qwiic | yes, one JST-SH connector, plain I2C on `Wire` |
| Antenna | U.FL socket **and** a through-hole `ANT` pad, selected by a jumper |
| Battery | JST-PH LiPo input with an MCP73831 charger, 500 mA default |

Read off this machine on 2026-08-21:

```
$ lsusb | grep 1b4f
Bus 001 Device 025: ID 1b4f:214f SparkFun SFE SAMD21 Pro RF

$ ls /dev/serial/by-id/
usb-SparkFun_SFE_SAMD21_Pro_RF_21A8043C5150484735202020FF09181F-if00 -> ../../ttyACM0
```

## Which revision this is, and why the product page will mislead you

**A SparkFun invoice dated 2019-03-06 lists this board as
`DD-15178` — "SparkFun Pro RF - LoRa, 915MHz (SAMD21, **Black Board**)",
$29.95.** That is a special run, not either of the catalogue SKUs, and it is
why searching for the part by name gets you the wrong page.

Three SKUs name this design:

| SKU | What it is | Status |
|---|---|---|
| `WRL-14916` | the original release, v1.0 | retired |
| `DD-15178` | **this board** — a black-PCB run, March 2019 | never in the main catalogue |
| `WRL-15836` | v1.1: VDDCORE fix, SWD pads, low-power LED jumper | current, $36.95 |

<https://www.sparkfun.com/products/15836> is the live product page for the
design. It is **not** proof of what is on this desk. Treat any revision-specific
claim from it as unconfirmed until it is read off the PCB.

This is the same trap the sibling ESP32 gateway project documents: SparkFun
migrated commerce platforms, and old SKUs now redirect to whatever the current
revision is. There, `products/15006` serves the WRL-18074 page and gets the
flash size wrong. Here, the retired `products/retired/14916` URL serves
WRL-15836 content. **A URL that resolves is not a URL that describes your
board.**

### What is confirmed for this board regardless of revision

The Qwiic connector is present — the 2019 sketches drive four Qwiic devices
through it and `scripts/board-check.sh` finds them. The pin map below is
identical across revisions.

### What is NOT confirmed

Whether this board has the VDDCORE fix or the SWD pads. Both are v1.1 changes.
If a debugger is ever wanted here, **look for the SWD pads on the PCB** rather
than assuming; a March 2019 special run could be built from either revision.

## The bootloader is not identified by its PID

**The bootloader does NOT change the USB PID on this board.** Measured
2026-08-21 — and this contradicts the PlatformIO board definition, which lists
`0x215f` as a second hwid. That PID has never been seen here:

| | Running a sketch | SAM-BA bootloader |
|---|---|---|
| VID:PID | `1b4f:214f` | `1b4f:214f` — **unchanged** |
| Manufacturer | `SparkFun` | **`Arduino LLC`** |
| Product | `SFE SAMD21 Pro RF` | `SparkFun SAMD21 Pro RF` |
| USB serial | the chip UID, `21A8043C…` | **absent** |
| `by-id` path | `usb-SparkFun_SFE_SAMD21_Pro_RF_21A8…-if00` | `usb-Arduino_LLC_SparkFun_SAMD21_Pro_RF-if00` |

It also usually comes back on the **same** `/dev/ttyACMn`. So neither the PID
nor a new tty appearing can be used to detect it — the first version of
`dump-flash.sh` waited for a new tty and timed out on a board that was already
sitting in the bootloader.

**The reliable test is the absence of a USB serial number**, because SAM-BA
does not read the SAMD21's chip UID:

```bash
udevadm info -q property -n /dev/ttyACM0 | grep -E 'ID_VENDOR=|ID_SERIAL_SHORT='
```

`ID_VENDOR=Arduino_LLC` and no `ID_SERIAL_SHORT` means the bootloader. The
scripts here use that test.

## The second board is dead — and how that was established

There were two Pro RFs: the sensor board, and the base station. **The base station is
dead.** Diagnosed 2026-08-23; recorded here because the failure signature is
specific and worth recognising again.

### What the kernel says

```
usb 1-6: new full-speed USB device number 67 using xhci_hcd
usb 1-6: device descriptor read/64, error -71
usb 1-6: device descriptor read/64, error -71
usb usb1-port6: attempt power cycle
usb 1-6: Device not responding to setup address.
usb 1-6: device not accepting address 69, error -71
usb usb1-port6: unable to enumerate USB device
```

**`-71` is `EPROTO` — a protocol error on the wire, not a refusal.** The host
sees a D+ pull-up, so the part is powered and its USB peripheral has attached.
The handshake then collapses, across four device numbers and a hub-initiated
power cycle.

### Why this is NOT a firmware problem

This is the opposite of the node's failure, which enumerated perfectly and hung
in `setup()`. Here nothing gets far enough for firmware to matter.

**The bootloader cannot rescue it.** SAM-BA is separate, protected, known-good
code, but it needs the same USB clock and the same data lines. If those are at
fault it fails identically — which is exactly what was observed.

### The two tests that settled it

1. **Same cable as the working board.** Not a similar cable — the identical
   one, moved across. Rules out the single most common cause of `-71`.
2. **Double-tap RESET produced no LED change at all.** On a healthy SAMD21 the
   bootloader pulses the pin-13 LED slowly and unmistakably. Nothing happened.

Together those say the chip is not executing, rather than executing something
broken. The likely causes are below the firmware layer: the 32.768 kHz crystal
(the SAMD21 derives its 48 MHz USB clock from the DFLL, normally closed-loop
against `XOSC32K`, and an off-spec clock fails enumeration at precisely this
stage), ESD damage to D+/D-, or a core power fault.

**Recovery would need SWD**, and only if the board has the pads — see below.

### VDDCORE is NOT the explanation

Worth writing down because it is a plausible-sounding lead that does not hold.

The v1.0 board has a documented VDDCORE defect: the pin was tied to 3.3 V
instead of decoupled to ground. It is a **power consumption** problem, about
60 mA awake against ~13 mA once corrected. SparkFun states it "doesn't affect
the overall functionality of the board."

So it does not cause enumeration failure. Do not spend time on it.

### It does give a revision test, though

**v1.0 boards are the ones without the SWD lines broken out.** So looking at
the PCB answers two questions at once: which revision it is, and whether SWD
recovery is even possible. This is the only reliable way to date a `DD-15178`,
whose SKU appears in no catalogue — see above.

### The `-71` signature, generally

| Kernel output | Meaning |
|---|---|
| nothing at all | not detected electrically — usually a charge-only cable |
| `device descriptor read/64, error -71` | attached, handshake failing — cable, USB clock, or damage |
| `New USB device found` but no `cdc_acm` | descriptors read, CDC not bound — a firmware problem, so the double-tap is worth trying |
| enumerates with a serial number | running a sketch |
| enumerates as `Arduino LLC`, no serial | SAM-BA bootloader, ready for an upload |

`scripts/usb-watch.sh` (`make usb`) captures and classifies this.

## Radio pin map

```c
constexpr uint8_t kPinRadioCs   = 12;  // NSS
constexpr uint8_t kPinRadioDio0 = 6;   // TxDone / RxDone -- the interrupt
constexpr uint8_t kPinRadioRst  = 7;   // active low
constexpr uint8_t kPinRadioDio1 = 10;
constexpr uint8_t kPinRadioDio2 = 11;
constexpr uint8_t kPinStatusLed = 13;

### Which LED is which

Five LEDs, and only one is under firmware control. From SparkFun's SAMD21 Pro
RF hookup guide:

| LED | Colour | Driven by |
|---|---|---|
| **D13 / STAT** | **blue** | **pin 13 — this is the one firmware controls** |
| PWR | red | power rail, always on (v1.1 has a jumper to disable it) |
| TX | green | serial traffic only |
| RX | yellow | serial traffic only |
| CHG | yellow | LiPo charging only |

**On battery away from USB, only two can be lit**: the red PWR steady, and the
blue D13 doing whatever the firmware is doing. TX and RX need serial traffic
and CHG needs a charger, so neither can confuse the reading. That is what makes
the blue LED usable as a field indicator: away from a serial cable it is the
only thing the board can tell you.
```

`lib/board/board.h` is the one definition. Everything else takes its pins from there.

**SPI is not a trap on this board.** The radio sits on the pins the SAMD21
Arduino core already uses for `SPI`, so plain `SPI.begin()` is correct and
nothing needs remapping. This is the opposite of the sibling ESP32 gateway,
where the radio is on the HSPI-side pins and every stock target's VSPI defaults
fail silently.

**What IS easy to miss** is that SparkFun's headline example names only two of
the five radio pins:

```c
RH_RF95 rf95(12, 6);   // chip select, DIO0.  RST, DIO1, DIO2 unmentioned.
```

RadioHead never touches RST, so a sketch written from that line alone cannot
put the radio into a known state — a warm restart finds the SX1276 in whatever
mode the previous run left it. The hookup guide's LMIC form is the complete one:

```c
const lmic_pinmap lmic_pins = { .nss = 12, .rst = 7, .dio = {6, 10, 11} };
```

Both firmwares here pulse pin 7 before `init()` for exactly that reason, and
`src/tools/probe` measures whether the pulse actually resets the part rather than
trusting the tutorial. (That test is worth running: on the ESP32 gateway next
door, *both* reset pins SparkFun publishes turned out not to be wired to the
radio at all.)

## The radio is the +20 dBm build — PA_BOOST only

RFM95**W** is the high-power variant. The RFO output path is not wired; every
milliwatt leaves through PA_BOOST.

**RadioHead's second argument is `useRFO`, and it must be `false`.**

```c
void setTxPower(int8_t power, bool useRFO = false);

rf95.setTxPower(14, false);   // false = do NOT use RFO, i.e. use PA_BOOST
rf95.setTxPower(14, true);    // WRONG -- routes output to an unconnected pin
```

**SparkFun's hookup guide prints the wrong one**, and so did the 2019 sketches
(`BaseStation.ino:96`, `ReceiverStation.ino`, `SMD21RFProRF-Client.ino` all
call `setTxPower(…, true)`). This project copied the idiom and inherited the
bug.

The symptom is total and silent: the radio initialises, reports no error,
`send()` returns true, the chip genuinely enters TX mode — and nothing is
received anywhere. There is no error at either end, because nothing is wrong
except which pin the power leaves by.

Confirmed 2026-08-21 by reading the register back with the `radio_regs` tool:

| | RegPaConfig | PaSelect | meaning |
|---|---|---|---|
| `setTxPower(14, true)` | `0x7E` | 0 | RFO — not wired |
| `setTxPower(14, false)` | `0x8C` | 1 | PA_BOOST — correct |

`board::kUseRfo` is named after the parameter it feeds, not after the intent,
precisely so this cannot be got wrong again by reading the call site.

Above 20 dBm the SX1276 needs `RegPaDac` set to its high-power value and has a
1 % duty cycle limit. Nothing here goes there; `board::kDefaultTxPowerDbm` is
14 dBm.

## Power

| | |
|---|---|
| USB | 5 V, and the on-board regulator feeds 3.3 V |
| `RAW` pin | **6 V absolute maximum** — the LDO is a low-dropout part, not a buck |
| LiPo | JST-PH, charged at 500 mA by default; use a ≥500 mAh cell |
| I/O | 3.3 V, and **not** 5 V tolerant. 7 mA source / 10 mA sink per pin |

Every Qwiic device on the chain draws from that same 3.3 V rail, and so does
the radio -- around 12 mA in continuous receive and far more in transmit. That
is why `rfm95::reset()` runs before the bus is probed: see `CLAUDE.md`.

## References

- Hookup guide — <https://learn.sparkfun.com/tutorials/sparkfun-samd21-pro-rf-hookup-guide>
- Product page (v1.1) — <https://www.sparkfun.com/products/15836>
- Retired v1.0 listing — <https://www.sparkfun.com/products/retired/14916> — **serves v1.1 content**
- Schematic (v1.1) — <https://cdn.sparkfun.com/assets/f/f/8/e/f/SAMD21_Pro_RF_v11_Schematic.pdf>
- Hardware repo — <https://github.com/sparkfun/SAMD21_Pro_RF/tree/v11>
- Graphical datasheet — <https://cdn.sparkfun.com/assets/e/0/6/6/5/SamProRF_Graphical_Datasheet_Updated.pdf>
- SAMD21 datasheet — <https://cdn.sparkfun.com/assets/6/3/d/d/2/Atmel-42181-SAM-D21_Datasheet.pdf>
- SX1276 datasheet — <https://cdn.sparkfun.com/assets/7/7/3/2/2/SX1276_Datasheet.pdf>
- SparkFun's paired LoRa examples — <https://github.com/sparkfun/SparkFun_SAMD21_Pro_RF_Arduino_Examples>
