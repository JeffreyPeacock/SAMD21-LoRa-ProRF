# Antenna

## What is attached

**Pycom LoRa & Sigfox Antenna Kit — 915 MHz**, SparkFun WRL-14676, from the
same 2019 order as the board. The same kit the sibling ESP32 gateway project
documents, and there is only one of it — the two boards cannot both have it.

| | |
|---|---|
| Type | whip, tilt/swivel — half-wave |
| Band | 868 / 915 MHz |
| Connector | **RP-SMA male** on the whip |
| Pigtail | RP-SMA female to U.FL / I-PEX MHF1, 1.13 mm coax, 100 mm |
| Whip length | ~200 mm |
| Gain | **not published** |

Pycom never published a datasheet and distributors leave the gain field blank.
Comparable 868/915 MHz half-wave whips are 2–3 dBi, but that is an inference
from similar parts. **Do not put a number in a link budget and call it
measured.** Same for VSWR and maximum power.

DigiKey's page survives as
[7721843](https://www.digikey.com/en/products/detail/pycom-ltd/SIGFOX-LORA-ANTENNA-KIT/7721843),
marked discontinued.

Also in the same order: **WRL-09145, Interface Cable SMA to U.FL**. That is a
plain SMA pigtail, not RP-SMA, so the Pycom whip will not screw onto it — see
the polarity note below.

## Do not transmit without one

The RFM95**W** is the +20 dBm build and PA_BOOST is its only output path. An
unterminated U.FL socket reflects essentially all the power back into the PA,
which has nowhere to dump it.

- `probe` and `base` **receive only** and are safe with nothing attached.
- `sensor` **transmits** and is not. `scripts/flash.sh node` warns before
  uploading.

The whip unscrews at the RP-SMA joint, leaving the 100 mm pigtail. **Receiving
on the bare pigtail is fine; transmitting on it is not.**

## The U.FL / ANT-pad jumper

The board has both a U.FL socket and a through-hole `ANT` pad, and the schematic
carries a jumper labelled *"Close if using U.FL connector"*. Only one path is
connected at a time.

**Check which way it is set before concluding the radio is deaf.** A board
transmitting into a disconnected path has the same symptoms as one with no
antenna — and the same risk to the PA.

## Connector polarity

The Pycom kit is **RP-SMA**, not SMA. They mate mechanically and do not work:
RP-SMA reverses which half carries the centre pin. A replacement whip must be
**RP-SMA male**. Plenty of 915 MHz LoRa antennas are sold as plain SMA, and
buying one is the usual way to lose an evening.

## Two bench antennas worth buying

The Pycom whip is 200 mm and rigid, which is what makes it good outdoors and
awkward on a desk. Both options below screw onto the **RP-SMA female pigtail
that is already attached**, so there is nothing to solder and nothing to
re-plug at the U.FL end.

### 1. SparkFun WRL-14875 — 110 mm rubber duck, 2 dBi

<https://www.sparkfun.com/915mhz-lora-antenna-rp-sma-1-4-wave-2dbi.html>

| | |
|---|---|
| Length | **110 mm** — against the Pycom's 200 mm |
| Gain | 2 dBi |
| Connector | RP-SMA |
| Price | $18.95, in stock (checked 2026-08-21) |
| Build | rubber duck: flexible, survives being knocked |

The low-risk choice. Same vendor ecosystem as the board, same connector family
as the pigtail, and a published gain figure — which the Pycom kit does not
have, so a link budget can be calculated rather than guessed.

### 2. Eightwood 3.5 dBi tilt-and-swivel, RP-SMA male, 2-pack

<https://www.amazon.com/Eightwood-Antenna-Connector-Direction-Wireless/dp/B07H5C5PM9>

Hinged, so it folds flat along the bench instead of standing up into whatever
is above it, and a 2-pack covers both radios.

**Specifications other than the title are unverified.** Amazon's product pages
render their detail tables in JavaScript, so length, price and VSWR could not
be read. Confirmed from the title only: 868/900/915 MHz, 3.5 dBi, **RP-SMA
male**, 2 per pack. Check the length before ordering — "tilt and swivel" covers
everything from 50 mm to 200 mm.

### Two things that will cost you an evening

**RP-SMA is not SMA.** They mate mechanically and do not work: RP-SMA reverses
which half carries the centre pin. The Pycom pigtail is RP-SMA **female**, so a
replacement must be RP-SMA **male**. A large share of "915 MHz LoRa antenna"
listings are plain SMA — including WRL-09145 in the same 2019 invoice.

**U.FL is rated for about 30 mating cycles.** It is a snap-fit connector meant
to be installed once at the factory, not a lab connector. So: **mate the U.FL
pigtail once and leave it there permanently, and do all antenna swapping at the
RP-SMA end.** This is the real argument for buying an antenna that screws onto
the existing pigtail rather than one that arrives with its own U.FL cable — the
latter is fine as a permanent per-board install, and wrong for a bench where
things get changed.

## Bench alternatives

### A wire quarter-wave on the ANT pad — free

SparkFun's hookup guide gives the lengths:

| Frequency | Length |
|---|---|
| **915 MHz** | **3.07 in / 78 mm** |
| 868 MHz | 3.38 in / 86 mm |
| 434 MHz | 6.47 in / 164 mm |

22 AWG solid core soldered to `ANT`, sticking straight up. Set the jumper for
the pad, not the U.FL socket.

At 915.0 MHz the quarter wavelength is 81.9 mm in free space; 78 mm is the
usual ~0.95 velocity-factor trim and is close enough that the difference is
lost in the ground-plane effects of a board this size. A quarter-wave whip with
no radial system is not a well-defined antenna anyway — the board and the USB
cable are half of it.

### Attenuate instead

For two boards a metre apart the problem is usually too much signal. At 14 dBm
into a receiver on the same desk the front end can saturate, and the symptom —
packets failing CRC when close and succeeding further away — reads as a bug.

`board::kDefaultTxPowerDbm` is 14. The driver accepts down to 2 dBm. One
constant, one rebuild, and it costs nothing.

## For the deployment

Keep the Pycom kit on whichever end moves. A tilt-swivel half-wave on 100 mm of
coax is a reasonable outdoor antenna and its length is why it works.

If it is replaced, the specification to match is 915 MHz half-wave or better,
RP-SMA male, and — unlike the Pycom — with a published gain figure and VSWR
curve, so a link budget can be calculated rather than guessed.
