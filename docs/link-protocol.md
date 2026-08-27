# The link — radio settings and wire format

Everything a second implementation needs in order to decode these packets
without reading the source.

## Radio settings

| | Value | Set where |
|---|---|---|
| Frequency | **915.0 MHz** | `board::kDefaultFrequencyMhz` |
| Bandwidth | 125 kHz | RadioHead default `Bw125Cr45Sf128` |
| Spreading factor | 7 | ditto |
| Coding rate | 4/5 | ditto |
| Header | explicit | ditto |
| CRC | on | ditto |
| Preamble | 8 symbols | SX1276 reset default |
| **Sync word** | **0x12** | SX1276 reset default — RadioHead never writes `RegSyncWord` |
| TX power | 14 dBm, **PA_BOOST** | `board::kDefaultTxPowerDbm` |

Two facts worth stating plainly, because they are the difference between two
radios hearing each other and two radios sitting in silence with no error on
either end:

**`Bw125Cr45Sf128` is applied by `RH_RF95::init()` itself**, not merely left at
the chip's reset value — `init()` calls `setModemConfig(Bw125Cr45Sf128)`,
`setPreambleLength(8)`, `setFrequency(434.0)` and `setTxPower(13)` before
returning. That is why the 2019 sketches worked without configuring the modem,
and why nothing here calls `setModemConfig()` either. It is a default being
relied on, not an absence of a decision — which is why it is written down.

The register bytes are `RegModemConfig1 = 0x72`, `RegModemConfig2 = 0x74`:
Bw `0111` = 125 kHz, CodingRate `001` = 4/5, explicit header, SpreadingFactor
`0111` = SF7, CRC on. Symbol time 1.024 ms; effective bit rate 5468.75 bps.

**The 2019 firmware used the same air interface.** Its RadioHead table was read
out of the flash at `0xec20` and its first two bytes are identical. The only
difference is `RegModemConfig3`: the 2019 library wrote `0x00` (**AGC off**),
1.120 writes `0x04` (**AGC on**). That changes receive behaviour, not
compatibility.

**0x12 is the private-LoRa sync word, not the LoRaWAN one.** LoRaWAN uses 0x34.
A LoRaWAN gateway will not hear these packets, and that is deliberate: this is
a point-to-point link, not a LoRaWAN device.

### Why 915.0 MHz — settled by reading it off the board

The three 2019 sketches disagreed. `BaseStation.ino` set 915.0,
`ReceiverStation.ino` and `SMD21RFProRF-Client.ino` set 921.2, and every one of
them printed a startup banner claiming a *third* number — `"RFM Client!: 903.9"`
appears above `float frequency = 921.2`. Two radios on different frequencies
hear nothing and report no error, which is exactly as silent as a wrong pin map.

**The tie was broken by the hardware.** The 2019 firmware dump holds the
IEEE-754 float `915.0` at flash offset `0xf588`, in the initialised-data image
— exactly where a file-scope `float frequency = 915;` is placed. The only other
frequency-shaped constant in the image is `434.0`, and that sits in a literal
pool inside `RH_RF95::init()`: RadioHead's own default, not a choice anyone
made.

So the last working pair was `BaseStation.ino` and this node, both on 915.0.
The 921.2 files are the earlier, abandoned branch. See
the recovered 2019 image.

915.0 MHz is the centre of the 902–928 MHz US ISM band and is clear of the ESP32
gateway on 903.9 MHz. `board.h` is the only place it appears.

### Time on air

SF7 / BW125 / CR4-5, 8-symbol preamble, explicit header, CRC on:

| PHY payload | Airtime | Duty cycle at one packet / 3 s |
|---|---|---|
| 24 B (an ACK) | 61.7 ms | 2.1 % |
| 100 B | 174.3 ms | 5.8 % |
| **158 B (a full telemetry record)** | **256.3 ms** | **8.5 %** |
| 251 B (the maximum) | 394.5 ms | 13.1 % |

The same 158-byte packet at SF9 is 820 ms and at SF12 is **5.9 seconds**. That
is the trade for range, and it is why `kTxIntervalMs` and the spreading factor
cannot be chosen independently — SF12 at a 3-second interval would be
transmitting essentially all the time.

**On US regulations:** 902–928 MHz digital modulation under FCC §15.247 expects
either frequency hopping or ≥500 kHz of occupied bandwidth. A fixed-channel
125 kHz LoRa link is neither, which is the usual state of hobby LoRa gear and
of every SparkFun example. It is recorded here as a known deviation, not
presented as compliant.

## Frame layout

Little-endian, packed, no padding. Both ends are the same SAMD21, so alignment
is not in question today; the layout is fixed so that it stays decodable by
something that is not.

These structures sit **inside** RadioHead's payload. RadioHead prepends its own
4 bytes — `to`, `from`, `id`, `flags` — which this protocol does not use for
addressing (all four are left at RadioHead's broadcast defaults). Sequencing is
in the application header instead, because RadioHead's `id` is 8 bits and wraps
every 256 packets.

### Header — 8 bytes, on every packet

| Offset | Size | Field | Value |
|---|---|---|---|
| 0 | 1 | `magic` | `0x4A` (`'J'`) |
| 1 | 1 | `version` | `0x01` |
| 2 | 1 | `type` | 1 telemetry, 2 ack, 3 hello |
| 3 | 1 | `nodeId` | 1 for the sensor board, 0 for the base |
| 4 | 4 | `seq` | uint32 LE, monotonic |

`seq` starts at 0 on every boot and does not persist. A reboot therefore looks
to the base like a large negative jump, which is visible rather than silent.

### Telemetry — node → base

Header, then a NUL-terminated ASCII record of at most 242 bytes.

The record is byte-identical to the line the sensor board wrote to its own SD card. That
is the point: the node's `SENSOR.LOG` and the base's `BASE.LOG` can be diffed
directly, and every line present in one and missing from the other is a packet
that was lost on the air.

```
$PJPT,<seq>,<ms>,<tempC>,<rh%>,<hPa>,<baroAltM>,<co2ppm>,<tvocppb>*HH
```

`$P` is NMEA 0183's reserved prefix for proprietary sentences and `JP` is this
project's manufacturer code, so these cannot collide with a standard sentence
and a generic NMEA parser skips them rather than choking. `*HH` is the
standard NMEA checksum: XOR of every character between `$` and `*`.

**An absent field is empty, not zero.** `,,` means "no reading"; `,0.00,` means
"measured zero". The 2019 code could not tell those apart, and a missing CCS811
looked like a room with no CO2 in it.

### Ack — base → node, 20 bytes

| Offset | Size | Field | |
|---|---|---|---|
| 0 | 8 | `header` | `seq` = the sequence being acknowledged |
| 8 | 4 | `highest` | highest sequence the base has seen |
| 12 | 4 | `received` | distinct packets the base has accepted |
| 16 | 2 | `rssi` | int16 LE, dBm, **base-side** |
| 18 | 1 | `snr` | int8, dB, base-side |
| 19 | 1 | `reserved` | 0 |

`highest - received + 1` is the number of packets lost, and the sensor board logs it.

**`rssi` here is the base's measurement of the node**, which the sensor board cannot
make for itself — a transmitter has no way to know how well it was heard. The
node separately records its own `lastRssi()` for the ACK, which is the reverse
direction. The two differ whenever the ends have different antennas, transmit
power or noise floors, which is always. Both go in `$PJPA`.

## Serial and log sentences

Everything either firmware emits is one of these, so a log is parseable with a
`split(',')` and no state.

| Sentence | From | Meaning |
|---|---|---|
| `$PJPT,…` | node | telemetry record (also what is transmitted) |
| `$PJPA,seq,highest,received,upRssi,upSnr,downRssi` | node | an ACK arrived |
| `$PJPR,seq,received,expected,missing,rssi,snr` | base | a packet arrived, with running loss |
| `$PJPH,quietSecs,received,highest` | base | nothing heard for 10 s — alive, not deaf |
| `$PJPB,role,id,boot` | both | startup marker |
| `$PJPX,foreign,len,rssi,snr` | base | something else is on this frequency |
| `$PJPE,…` | node | internal error |

`$PJPH` exists because a silent base and a broken base look identical, and the
2019 base station printed a `HEARTBEAT` line on every failed 1-second receive
window — hundreds of lines an hour, into the same SD card that was supposed to
hold the data.

`$PJPX` exists because "the sensor board is dead" and "someone else's traffic is on this
channel" are different problems with the same symptom.

## Decoding these on a third radio

The base station Pro RF is dead, so the ESP32 gateway in
`../ESP32-LoRa-1Ch-Gateway` is the candidate receiver. Its minimal SX1276
driver already defaults to **sync word 0x12, SF7, BW125, CR4/5** — the same
settings as this link, leaving only the frequency to change. See
`docs/receiving-elsewhere.md`.
