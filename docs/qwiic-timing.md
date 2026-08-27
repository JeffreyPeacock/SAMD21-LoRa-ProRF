# Qwiic bus timing — measured, not assumed

Benchmarked with `make flash-qwiic_bench DEV=<name>`, one device at a time on
an otherwise idle bus. Done this way on the repository owner's advice, after an
attempt to bring all five up at once turned into a debugging session with too
many variables.

## The headline: the bus needs no rest

400 kHz, 100 transactions per gap setting:

| Device | Address | Bytes read | µs / transaction | Failures at **0 µs** gap |
|---|---|---|---|---|
| BME280 | `0x77` | 1 (reg `0xD0`) | **109** | 0 / 100 |
| CCS811 | `0x5B` | 1 (reg `0x00`) | **116** | 0 / 100 |
| OpenLog | `0x2A` | 1 | **138** | 0 / 100 |

The spacing sweep ran 20 ms, 10, 5, 2, 1 ms, then 500, 200, 100, 50 and 0 µs
between the end of one transaction and the start of the next. **Every device
was 0/100 failures at every gap, including none at all.**

So inter-transaction spacing is not a constraint on this chain, and a slot
scheduler does not need guard time between devices. That is a useful negative
result: it means any reliability problem seen later is NOT bus contention, and
time spent tuning gaps is time wasted.

At 100 kHz the same BME280 transaction costs 386 µs against 109 µs, consistent
with the 4x clock. There is no reliability argument for the slower speed.

## What actually costs time

A full cycle is well under a millisecond of bus time. **The bus is not the
constraint and never was** — which is the whole point of measuring rather than
assuming.

Nothing here justifies a 250 ms slot. **The slot length is set by device
conversion rates, not by the bus**: the CCS811 produces one result per second
in its default drive mode, and that alone sets the useful cycle period.

## The OpenLog cannot keep up at 400 kHz, and it fails by lying

The single most expensive finding of the session, because the failure never
looks like what it is.

Measured with `src/tools/openlog_test`, one device on an idle bus:

| Bus speed | `getStatus()` | `getVersion()` | Bytes actually written |
|---|---|---|---|
| 400 kHz | `0xFF` | `255.255` | **0** |
| 100 kHz | `0xFF` | `2.0` | 206 |
| **50 kHz** | **`0x1D`** | **`2.0`** | **380 — correct** |

`0xFF` is not a status byte. It is an idle bus read back through the pull-up.

**The device ACKs its address at every speed**, because the OpenLog is an
ATmega328 whose TWI hardware acknowledges its own address without the firmware
being involved. The firmware is simultaneously driving an SD card over SPI. So
at 400 kHz the board looks present, `begin()` succeeds, writes return without
error — and nothing reaches the card.

That is why this presented in three different disguises over several hours:
"the OpenLog is absent", "the OpenLog left the bus", and "writes succeed but
the file is empty". All one cause.

**The fix is per-transaction, not per-bus.** The rest of the chain wants
400 kHz, which suits the sensors, so `lib/datalog`
drops to 50 kHz around OpenLog transactions and restores it afterwards.
`Wire.setClock()` only rewrites a baud register and costs nothing next to an SD
write.

`datalog::begin()` now VERIFIES rather than assumes: a version string of
`255.255`, or a status of `0xFF`, or `SD_INIT_GOOD` clear, all mean not usable,
and it says so instead of reporting success.

### The cost, measured

SD writes are **283 ms mean, 404 ms worst** — and they BLOCK. The 2019 firmware
measured 3–310 ms for the same operation, so this is the device, not the
approach.

That exceeds the 250 ms slot, so logging distorts the cycle: a 27 s run
completed 20 cycles rather than 27. It does not lose records — every one of 58
was logged — but a 283 ms block is 283 ms in which nothing else on the chain
is being read, and a streaming device would lose whatever arrived meanwhile.

Options: lengthen the slot and accept a longer cycle, or put the OpenLog on the
second I2C bus (`SERCOM2` on D3/D4) so a slow write cannot starve the sensors
at all. **Batching is not one of them** -- it was measured and it makes the
blocking window an order of magnitude worse. See `CLAUDE.md`.
