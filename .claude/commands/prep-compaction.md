# Prepare this session's work for compaction

Fold what was learned into the documents before the context that produced it is gone. Follow the
steps in order.

The risk is specific: on this project the valuable output is almost always a **finding** rather than
a code change — that `setTxPower`'s second argument is `useRFO` and not "use PA_BOOST", that the
OpenLog returns `0xFF` to every read at 400 kHz while still ACKing, that its version register is
unreliable enough that a single read must not gate anything. Those live only in the conversation
until someone writes them down.

## Step 1 — What actually happened

```bash
git log --oneline -20
git status --short
```

Separate three things, because they are documented differently:

- **Findings** — something is true that we did not know. `CLAUDE.md`, or a `docs/` file if long.
- **State changes** — which firmware is on which board, what is wired, what is broken. README
  "Current state".
- **Narrative** — how we got there. Commit messages, not living documents.

## Step 2 — Update CLAUDE.md with findings only

Targeted edits. Do not rewrite sections that did not change.

**Look hardest for statements that are now wrong.** A stale claim is worse than no claim, because a
later session will act on it. This project has already had to correct: the bootloader's USB PID, the
transmit frequency, a clock that reported a source it did not have, and three separate
theories about the OpenLog.
**Correcting a stale claim outranks adding a new one.**

Watch the size — 40k is the limit. Use `/trim-claude-md` if it is over 30k.

## Step 3 — Update the README's "Current state"

If the session changed what is flashed, what is wired, or what works, those sections are now lying.
**The devices are authoritative — read them rather than reconstructing from memory:**

```bash
make board                    # host, USB, port, permissions, what it prints
BOARD=base make board
./scripts/capture.sh 12       # what the sensor board is actually saying
```

Record which board holds which firmware. There are two, they are the same model, and only the chip
UID in the USB serial number tells them apart.

## Step 4 — Regenerate what is generated

```bash
make check                    # 13 environments, 19 host tests, shellcheck
```

A commit that does not build is worse than no commit. This has happened once: the tree was committed
after building only the two environments that had changed.

## Step 5 — Check nothing sensitive is about to be committed

A session log picks up identifying material incidentally — a shell prompt, an invoice opened while
ordering parts, a pasted email.

```bash
git grep -nE '[0-9]{2}\.[0-9]{5,}, *-?1[0-9]{2}\.[0-9]{5,}' -- . | head
git status --short --untracked-files=all
```

Confirm `etc/firmware/`, `.claude_artifacts/` and `docs/datasheets/` are still ignored. The 256 KB
flash dump is deliberately **not** committed — that is settled.

## Step 6 — Optionally export the transcript

If the session found things worth keeping verbatim:

```bash
./scripts/export-transcript.sh
```

Masking is on by default and covers coordinates, raw NMEA fixes, email and street addresses.

## Step 7 — Commit on main

```bash
git branch --show-current      # must be main
```

There is no remote, so commit locally and say so rather than reporting a push that did not happen.

Stage only what this command changed. **No AI attribution** — standing instruction.

Write the message to explain *why* something is true, not just that it changed. The commit log is
where the narrative belongs, and it is the only place a future reader can recover the reasoning.

## Step 8 — Report

What was updated, what was deliberately left alone, and — most importantly — any **stale claim you
corrected**, since that is the thing most likely to have misled someone later.
