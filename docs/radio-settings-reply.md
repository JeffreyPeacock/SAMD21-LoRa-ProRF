# Radio settings — getting a message from a Pro RF to the ESP32 gateway

Answering `../ESP32-LoRa-1Ch-Gateway/docs/radio-settings-request.md`.

Direction: **the Pro RF transmits, the gateway receives.**

Everything here is about making a message cross that link and be *decoded* at
the far end. It is deliberately agnostic about what the message says.

## The five

```
Frequency        : 915.0 MHz        (exactly; Frf = 0xE4C000)
Spreading factor : SF7
Bandwidth        : 125 kHz
Coding rate      : 4/5
Sync word        : 0x12             (private LoRa)
```

Set in `lib/board/board.h` (`kDefaultFrequencyMhz`) and by RadioHead's
`Bw125Cr45Sf128`, which `RH_RF95::init()` applies itself — it is a default
being *set*, not a chip default being left alone.

**The sync word is never written by RadioHead.** `RH_RF95_REG_39_SYNC_WORD` is
defined in its header and referenced nowhere in `RH_RF95.cpp`, so the register
keeps the SX1276 reset value, `0x12`. That is the right answer for a private
link, but it is a default nobody chose — worth knowing before someone "fixes"
it.

## They already match, at register level

Both radios are SX1276s, so this can be compared as register writes rather than
as intentions. Gateway values computed from `lib/sx1276/sx1276.cpp` with its
`lib/board/board.h` config; ours from RadioHead 1.120's `MODEM_CONFIG_TABLE[0]`.

| Register | Pro RF (RadioHead) | Gateway (its own driver) | |
|---|---|---|---|
| `RegFrf` 0x06–08 | `0xE4C000` | `0xE4C000` | match |
| `RegModemConfig1` 0x1D | `0x72` | `(7<<4)\|(1<<1)\|0` = `0x72` | match |
| `RegModemConfig2` 0x1E | `0x74` | `(7<<4)\|0x04` = `0x74` | match |
| `RegModemConfig3` 0x26 | `0x04` | `0x00\|0x04` = `0x04` | match |
| `RegPreamble` 0x20–21 | `0x0008` | `0x0008` | match |
| `RegSyncWord` 0x39 | `0x12` (reset default) | `0x12` (written) | match |

Decoded: Bw `0111` = 125 kHz, CodingRate `001` = 4/5, ImplicitHeaderModeOn `0`
= **explicit header**, SpreadingFactor `0111` = SF7, RxPayloadCrcOn `1` =
**CRC on**, LowDataRateOptimize `0` (correct at SF7 — a symbol is 1.024 ms,
well under the 16 ms threshold), AgcAutoOn `1`.

`915000000 × 2^19 / 32000000 = 14991360 = 0xE4C000` exactly, with no rounding,
so both ends land on the same synthesiser word.

**Nothing needs changing on either side.** The gateway was on 903.9 MHz earlier
in this project's history; it is on 915000000 Hz now.

### Why 915.0 and not something else

It is not a preference. The 2019 firmware still on the Pro RF holds the
IEEE-754 float `915.0` at flash offset `0xf588`, recovered with
`scripts/dump-flash.sh`. Staying on 915.0 means the gateway can hear **both**
this project's firmware and the original 2019 image if that is ever reflashed.

## What the request does not ask for, and you will need

The five settings get the receiver to demodulate. They do not get it to
*decode*. The gateway's `SX1276::Packet.data` is the raw LoRa payload, and the
first twelve bytes of ours are framing:

```
byte  0     0xFF   RadioHead header: "to"    (RH_BROADCAST_ADDRESS)
byte  1     0xFF   RadioHead header: "from"  (RH_BROADCAST_ADDRESS)
byte  2     0x00   RadioHead header: "id"
byte  3     0x00   RadioHead header: "flags"
bytes 4-11         proto::Header  -- magic 0x4A ('J'), version 0x01,
                   type, nodeId, then seq as uint32 little-endian
bytes 12-          the ASCII record, NUL-terminated:  "$....*HH"
```

`RH_RF95::send()` writes those four header bytes into the FIFO before the
payload and sets `RegPayloadLength = len + 4`. They are unavoidable while the
Pro RF uses RadioHead, and they are **not** part of this project's protocol —
addressing is in `telemetry::Header` instead, because RadioHead's `id` is 8 bits
and wraps every 256 packets.

So, to read a telemetry record on the gateway:

```c
if (pkt.length > 12 && pkt.data[4] == 0x4A && pkt.data[5] == 0x01) {
    pkt.data[pkt.length - 1] = '\0';          // defensive
    const char* record = (const char*)&pkt.data[12];
    uint32_t seq;  memcpy(&seq, &pkt.data[8], 4);   // little-endian
    // record is a checksummed NMEA-style sentence, "$...*HH"
}
```

Skipping twelve bytes and printing the rest as a C string gets the sentence out
with no parser at all. **That is why the record is ASCII rather than packed
binary** — it costs airtime and buys exactly this.

Full frame layout: `docs/link-protocol.md`.

### Sentence checksums: two different rules in play

Sentences from this project use **standard NMEA 0183** checksums — XOR of
everything between `$` and `*`, printed as `%02X`.

The **2019 firmware does not**. Its sentences (`$XXWTH`, `$XXCO2`) XOR the
leading `$` *into* the value and print it in decimal, `*%d`. Confirmed 82/82
against captured traffic. If the gateway is ever pointed at the original
firmware, a standard checksum test will reject every sentence. See

## Correction, 2026-08-21: we were transmitting into an unconnected pin

If you listened before this date and saw `0 received` with a clean noise floor,
that was us, not you. **The transmit path was routed to RFO, which the RFM95W
does not connect.**

RadioHead's signature is `setTxPower(int8_t power, bool useRFO = false)`. The
flag selects RFO. SparkFun's hookup guide prints `setTxPower(14, true)`, the
2019 sketches used it, and this project copied it — so the output went to a pin
that goes nowhere.

Nothing reported an error. The radio initialised, `send()` returned true, the
chip genuinely entered TX mode, and the console said "sent". Read back from the
chip with our `radio_regs` tool:

| | RegPaConfig | PaSelect | |
|---|---|---|---|
| before | `0x7E` | 0 | RFO — not wired |
| after | `0x8C` | 1 | PA_BOOST — correct |

Everything else was already right and is unchanged: `RegFrf = 0xE4C000` =
exactly 915.0 MHz, `RegSyncWord = 0x12`, `RegModemConfig1/2/3 = 0x72/0x74/0x04`
= SF7 / BW 125 kHz / CR 4/5 / explicit header / CRC on, preamble 8 symbols.

**Worth checking on your side too**, since the same idiom is all over the
SparkFun and Adafruit examples: if your driver takes a `useRFO`-style flag,
confirm it is false, and read `RegPaConfig` back rather than trusting the call.
Bit 7 must be 1.

## Two operational points

**There is one antenna and the transmitter must have it.** The Pycom 915 MHz
kit is documented in both projects because there is a single unit shared
between them. Receiving with a bare U.FL socket is harmless; **transmitting
into one can damage the PA** on an RFM95W, which is PA_BOOST-only. Since the
Pro RF transmits, the Pycom kit goes on the Pro RF, and the gateway receives
without one — fine over a short distance. **Check the band and the connector
polarity** before substituting anything: an antenna cut for another band is
worse than none, and the kit is RP-SMA while plain SMA parts will not mate.
See `docs/antenna.md`.

**Turn the power down for bench testing.** The Pro RF transmits at
`board::kDefaultTxPowerDbm = 14` dBm. Two radios on one desk at 14 dBm can
saturate the receiver's front end, and the symptom — packets failing CRC when
close and succeeding further away — reads as a bug rather than as overload.
RadioHead accepts 2 dBm; the gateway's driver clamps to 2..17. One constant,
one rebuild.

## Expected numbers, so a bad link is recognisable

At SF7 / BW125 / CR4-5, 8-symbol preamble, explicit header, CRC on:

| | |
|---|---|
| Symbol time | 1.024 ms |
| Effective bit rate | 5468.75 bps |
| Airtime, a full-size record (158 B PHY) | **256 ms** |
| Airtime, an ACK (24 B) | 62 ms |
| Receiver sensitivity | ≈ −123 dBm |
| SNR limit | −7.5 dB |

On a bench a metre apart, expect RSSI around −30 to −50 dBm and SNR near
+9 dB. An RSSI below −110 dBm with SNR under −5 dB means the link is at its
limit and packet loss should be expected.

**The gateway cannot ACK.** Nothing on that side speaks `telemetry::Ack`, so
every packet goes unacknowledged. That is expected and harmless here: the
transmitter never waits for an acknowledgement and its behaviour does not
depend on one. What is lost is only the receive-side RSSI figure, which a
transmitter cannot measure for itself.

---

## Scope of this copy

This is the **public copy**, kept with the SAMD21 Pro RF board-support work. It
covers only what two radios need in order to exchange a message: modem
settings, the RadioHead framing that sits in front of the payload, checksum
rules, antenna and power constraints, and the airtime arithmetic.

It deliberately says nothing about what is being measured or why. The original,
written against a specific application, is retained privately.
