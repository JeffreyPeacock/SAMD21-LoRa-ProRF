# Export this session's transcript

Render the Claude Code session log as readable text with secrets masked, using
`scripts/export-transcript.sh`.

`$ARGUMENTS` may contain a date, a session UUID, or an output path. Parse what is
there and ask only for what is genuinely ambiguous.

## Run it

```bash
./scripts/export-transcript.sh
```

That exports the **most recent** session to
`.claude_artifacts/transcripts/YYYY.MM.DD-transcript.txt`, masking as it goes.
The whole of `.claude_artifacts/` is gitignored, and the script refuses to write
anywhere inside the repository that is not ignored.

Useful variants:

```bash
./scripts/export-transcript.sh --list                  # what sessions exist
./scripts/export-transcript.sh --session <uuid>        # a specific one
./scripts/export-transcript.sh --out /tmp/t.txt        # somewhere else
./scripts/export-transcript.sh --no-thinking           # drop reasoning blocks
```

## Masking is the point — do not casually disable it

**This project's secrets are not the gateway's.** That board printed WiFi
credentials on every boot; this one has no WiFi and stores no credentials.

What is worth masking here is anything identifying a **person or a place**. A
session log picks those up incidentally — from a shell prompt, an invoice
opened while ordering parts, a pasted email. `scripts/lib/mask.py` therefore
masks:

| | |
|---|---|
| `coordinates` | a decimal-degree pair anywhere — code, comments, prose |
| `email` | any address |
| `street-address` | an address as an order confirmation writes it |
| `token`, `private-key` | generic credential shapes, in case one ever appears |

It deliberately does **not** mask RSSI, SNR, sequence numbers, or sensor
readings. Those are the point of the transcript, and over-masking a log until
it is unreadable defeats the exercise.

Add a project-specific literal — a exact string that no pattern will catch — to
`.claude_artifacts/mask-secrets.txt`, one per line.

## Check the output before sharing it

```bash
grep -nE '[0-9]{2}\.[0-9]{5,}' <output>     # any surviving coordinates?
```

The whole of `.claude_artifacts/` is gitignored, and the script refuses to write
anywhere inside the repository that is not ignored.
