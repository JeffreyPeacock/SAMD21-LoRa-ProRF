# Trim CLAUDE.md

Bring `CLAUDE.md` back under the 40k character performance limit without losing anything expensive
to rediscover.

## Step 1 — Measure

```bash
wc -c CLAUDE.md
```

Under 30k: report the size and stop. **Cutting a file that is not too large only loses information.**

Measure rather than assume — this file climbs faster than it feels like it should, because almost
every session here produces at least one finding that has to be written down or lost.

## Step 2 — Triage before touching the file

Assign every candidate passage to a destination **before** editing:

| Bucket | Criteria | Action |
|---|---|---|
| **Delete** | One-time setup notes for work already done; the narrative of how something was discovered once the finding itself is stated; anything derivable from the code in front of you | Remove |
| **Condense** | A finding that matters but is explained at three times the length it needs | Rewrite to a sentence or two, keeping the *fact* and the *consequence* |
| **Move to README** | Orientation, procedure, or rationale a human arriving cold would want | Add to `README.md` first, then remove |
| **Move to docs/** | Reference too long for README but too valuable to lose | New `docs/<subject>.md`, linked from README |
| **Save to memory** | Cross-project lessons about how to work, not about this repo | Write the memory file first, then remove |

**Never delete without assigning a destination.** Record the disposition in the Step 5 report.

### Never cut these

This file is mostly hard-won findings. Each cost real time and none is recoverable by reading the
code:

- **`setTxPower(power, useRFO)` — the flag selects RFO, not PA_BOOST.** SparkFun's own guide prints
  the wrong one, the 2019 sketches used it, and we transmitted into an unconnected pin with no error
  at any layer. `RegPaConfig` must read `0x8C`, not `0x7E`.
- **The frequency is 915.0 MHz**, read off the flash at `0xf588`, not the 921.2 the examples use.
- **The OpenLog cannot run at 400 kHz** — it ACKs its address while returning `0xFF` to every read,
  so writes "succeed" and the card stays empty.
- **A library `begin()` can hang `setup()` with USB still alive**, which looks exactly like a
  sketch that prints nothing. Unbounded `while (...)` drain loops are the usual cause.
- **Neither the PID nor a new tty identifies the bootloader** — both states report `1b4f:214f`. The
  test is the *absence* of `ID_SERIAL_SHORT`.
- **A single I2C probe is not a presence test**, and **the radio must be reset before probing I2C**.
- **The OpenLog's version register is unreliable** — two reads minutes apart gave `2.255` and
  `255.0`. Gate on a functional write, never on an ID read.
- **An unguarded `#define` shared by two libraries can change a class layout** — an ODR violation
  the linker cannot see, whose only symptom is a `redefined` warning that looks cosmetic.
- **newlib-nano's `printf` has no floating point** — a `%f` emits nothing. This has been hit twice.
- **The 2019 firmware's NMEA checksums are non-standard** — XOR including the `$`, printed decimal.
- **The second board is dead**, diagnosed and not to be re-diagnosed.
- **Port ownership**: `/dev/ttyACM0` and `/dev/ttyACM1` are ours; `ttyUSB*` belongs to others.
- **The no-attribution rule.**

Prefer condensing prose around a finding over deleting the finding.

## Step 3 — Preserve displaced content first

Write to README, `docs/`, or memory **before** removing anything from `CLAUDE.md`. If the process is
interrupted between the two, the version that loses nothing is the one where the copy already exists.

**Treat the repository as publishable.** Moving content to README exposes it. Check for coordinates
and the invoice address before doing so.

## Step 4 — Edit, then verify

Make targeted edits only. Re-measure:

```bash
wc -c CLAUDE.md
```

Target 30–35k, leaving headroom. If still over 40k, return to Step 2 rather than cutting deeper into
findings.

Check that every path the file mentions still exists:

```bash
grep -ohrE '(docs|src|scripts|lib|test|etc)/[A-Za-z0-9_./-]+' CLAUDE.md \
  | sed 's/[.,)]$//' | sort -u | while read -r f; do [[ -e "$f" ]] || echo "MISSING: $f"; done
```

## Step 5 — Report and commit

List every section touched and its disposition — deleted, condensed, moved to README, moved to
`docs/`, or saved to memory — with before and after sizes.

Commit on `main` with a `Docs:` prefix, staging only the files this command changed. **No AI
attribution.**
