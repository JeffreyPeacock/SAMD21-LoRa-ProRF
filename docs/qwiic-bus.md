# The Qwiic chain

Daisy-chained off the Pro RF's one Qwiic connector.

| Device | SKU | I2C address | What it gives |
|---|---|---|---|
| Environmental Combo — **BME280** | SEN-14348 | `0x77` (alt `0x76`) | temperature, humidity, pressure, barometric altitude |
| Environmental Combo — **CCS811** | SEN-14348 | `0x5B` (alt `0x5A`) | equivalent CO2 (ppm), TVOC (ppb) |
| **Qwiic OpenLog** | DEV-15164 | `0x2A` | microSD, append-only |

The two environmental sensors share one breakout board but are two separate I2C
devices at two addresses — which is worth knowing the first time a scan reports
one more device than there are boards on the desk.

Run `pio run -e probe -t upload` and the board scans the bus and names whatever
answers.

## Qwiic is one shared I2C bus

Not a hub, not a switch. Every device is on the same two wires, and the daisy
chain is electrical, not logical. Two consequences that both look like "the
sensor is broken":

**One bad link takes out everything past it.** A reversed or unseated cable
mid-chain removes every device downstream of it. If the probe finds the first
two devices and not the last three, suspect the cable between them, not the
three devices.

**Every device shares the 3.3 V rail from the Pro RF's regulator**, and so does
the radio. On USB there is headroom; on a small LiPo with
the radio transmitting at the same moment, less so.

## Rate limits

Each device has a rate beyond which polling only loads the bus:

| Device | Interval used | Why |
|---|---|---|
| BME280 | 500 ms | forced-mode conversion is a few ms; nothing changes faster |
| CCS811 | 1000 ms | its default drive mode produces one result per second |

**The slot length is set by conversion rates, not by the bus.** A full cycle is
well under a millisecond of actual bus time — see `qwiic-timing.md` — so a
250 ms slot is generous by three orders of magnitude, and shortening it would
only re-read values that have not changed.

A device that *streams* rather than answers is a different problem: it fills an
internal buffer and discards the overflow with no error anywhere, so it has to
be drained every pass through `loop()` rather than once per cycle. Nothing on
this chain behaves that way, but the scheduler is built so that one could be
added without redesigning it.

The bus runs at **400 kHz**, set in `board::kI2cClockHz` — with the OpenLog as
the documented exception, driven at 50 kHz per transaction.

## Library versions are pinned, and that is not optional

These are 2019 breakout boards whose libraries have had breaking changes since.
The clearest example: the 2019 sketch used

```c
CCS811Core::status returnCode = ccs811Sensor.begin();
switch (returnCode) { case CCS811Core::SENSOR_SUCCESS: ... }
```

In the v2 library that enum is `CCS811Core::CCS811_Status_e` and the value is
`CCS811_Stat_SUCCESS`, `begin()` returns `bool`, and the status-returning entry
point is `beginWithStatus()`. Copying the 2019 code into a current build does
not compile, and the error message points at the enum rather than at the
version.

`platformio.ini` pins every library. Versions in use:

```
mikem/RadioHead@1.120
sparkfun/SparkFun BME280@2.0.9
sparkfun/SparkFun CCS811 Arduino Library@2.0.3
sparkfun/SparkFun Qwiic OpenLog@3.0.2
```

**Exact tag strings matter.** SparkFun has changed tag conventions partway
through a repository's life -- `V_1.0.1` in one place and `v1.0.2` in another --
and only the exact string resolves. A near miss fails with "Remote branch not
found", which reads like the repository is gone rather than the tag.

## The OpenLog is optional, deliberately

`logger::begin()` probes `0x2A` and returns false if nothing answers; every
subsequent call becomes a no-op. The firmware says so at startup and keeps
running.

The 2019 code did the opposite — `while(1);` on any failed `begin()` — which
turned one loose cable into a board that stopped logging, stopped transmitting,
*and* could no longer be reflashed over USB, because a hung sketch cannot answer
the 1200-baud touch. See `docs/programming.md`.

Files are opened with `append()`, so a power cycle adds to the log rather than
erasing it, and `syncFile()` is called every 15 seconds. Data written but not
synced is lost if power drops, and this node is battery powered and carried
around: there is no orderly shutdown to sync at.

**Filenames are fixed** — `SENSOR.LOG` and `BASE.LOG`. The OpenLog's firmware is
FAT 8.3 short names only, and there is no RTC on the Pro RF, so there is no
clock to name a file after at boot -- the host can set the clock later, but not
before `setup()` runs. A `$PJPB` boot marker is written at the top of each
session instead.
