# Receiving these packets without a second Pro RF

The base station Pro RF is dead. Until it is replaced, `pio run -e base` has
nothing to run on, and the sensor board transmits into a channel nobody is listening to.

There are three ways out, in increasing order of work.

## 1. Run `base` on the sensor board itself — zero hardware, proves the firmware

`base` and `sensor` are separate environments for the same board. Flashing `base`
onto the surviving Pro RF proves the receive path, the sequence accounting, the
OpenLog writes and the serial output are all correct. What it cannot do is
prove anything about the link, because there is nothing transmitting.

Do this first anyway. It separates "the firmware works" from "the radio link
works", and those are the two things that get confused when a single board is
all there is.

## 2. The ESP32 gateway next door — the realistic path

`../ESP32-LoRa-1Ch-Gateway` is a SparkFun LoRa Gateway 1-Channel (ESP32),
WRL-15006, with an RFM95W — **the same SX1276 silicon as the Pro RF**. Its
project already has a minimal SX1276 driver, an `rx` environment that listens
and reports RSSI/SNR, and a `scan` environment that sweeps LoRa settings.

**Its driver's defaults already match this link on four of the five parameters
that matter.** From that project's `src/board.h`:

| Parameter | Gateway default | This link | Match? |
|---|---|---|---|
| Sync word | `0x12` | `0x12` | yes |
| Spreading factor | 7 | 7 | yes |
| Bandwidth | 125 kHz | 125 kHz | yes |
| Coding rate | 4/5 | 4/5 | yes |
| **Frequency** | **903.9 MHz** | **915.0 MHz** | **no** |

So the work is one constant. Point the gateway's `rx` environment at
915.0 MHz — or the other way round, and change `board::kDefaultFrequencyMhz`
here to 903.9 — and it should hear the sensor board.

915.0 is not an arbitrary pick: it is the value read out of the 2019 firmware
image, so a gateway set to it will also hear the *original* firmware if that is
ever reflashed.

**What it will show is a raw payload, not a decoded record.** The gateway's
driver knows nothing about RadioHead. What arrives at its receive callback is:

```
[0..3]   RadioHead header:  to, from, id, flags
[4..11]  proto::Header:     magic 0x4A, version 1, type, nodeId, seq (LE32)
[12..]   the ASCII record:  "$PJPT,…*HH", NUL-terminated
```

Skipping the first 12 bytes and printing the rest as a C string gets the
telemetry sentence out with no parser at all — which is precisely why the record
is ASCII rather than packed binary. That decision costs airtime and buys this.

**Do not confuse the two boards' pin maps.** Nothing from this project's
`board.h` applies over there and nothing from theirs applies here. That project
records its map as confirmed by reading `RegVersion` off the chip; it is
NSS 16 / SCK 14 / MISO 12 / MOSI 13 / DIO0 26, on the HSPI-side pins, and its
reset line does not work at all. Here it is CS 12 / DIO0 6 / RST 7 on the
default SPI bus. The only thing the two boards share is the radio silicon.

Also note that gateway board's own constraints, from its `CLAUDE.md`: its boot
banner prints stored WiFi credentials in the clear, and its `esptool` operations
need `--no-stub`. Neither affects receiving, but both affect working on it.

## 3. Buy a second Pro RF — $36.95

<https://www.sparkfun.com/products/15836>. This restores the design as intended
and gets the ACK path back, which options 1 and 2 cannot: the ESP32 gateway can
*hear* these packets, but nothing over there speaks `telemetry::Ack`, so the node
would run permanently unacknowledged.

The sensor board is built to tolerate exactly that — it never waits for the base — so an
unacknowledged run is a valid run. It just loses the base-side RSSI figure,
which is the one link measurement the sensor board cannot make for itself.

**Confirm the existing board is actually dead before spending anything.** The
same checks that apply to the live one apply to it:

```bash
./scripts/board-check.sh
```

A charge-only USB cable enumerates nothing at all and is indistinguishable from
a dead board at a glance. A hung sketch that never answers the 1200-baud touch
looks dead to `pio upload` but recovers with a double-tap of RESET. Both are
worth eliminating first — see `docs/programming.md`.

## What is not an option

**LoRaWAN.** Not because the hardware cannot — the Pro RF has LoRaWAN jumpers
on its underside and SparkFun ships a TTN ABP example — but because the only
gateway here is single-channel, and The Things Network v3 rejects
single-channel gateways. That would mean standing up a private network server
(ChirpStack) to receive from a link that already works point-to-point. It is a
larger project, not a shortcut to a working receiver.

**Meshtastic or Reticulum as a receiver.** Both would need custom board
definitions, and neither speaks this protocol. They are worth considering as a
*replacement* for this link, not as a way to listen to it.
