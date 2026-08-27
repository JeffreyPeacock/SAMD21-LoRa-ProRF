#!/usr/bin/env python3
"""Base station: set the clock, then monitor -- and hand records to sinks.

This is the deployment tool. The sensor board is placed at the site and
verified with a laptop; the base station is then carried to wherever it will
live, plugged in, and left. This script is what runs at that point.

It does three things, in order:

  1. Finds the base board by its SAMD21 chip UID. Not by /dev/ttyACMn, which
     is handed out in plug order, and not by "any Pro RF", which is a coin
     flip when two are attached.
  2. SETS THE CLOCK. The board has no RTC crystal and no backup cell, so it
     boots knowing only milliseconds since reset. It waits two minutes at
     startup for a clock; this sends one immediately. Every record it receives
     afterwards is tagged with real UTC instead of uptime.
  3. Reads records and hands each one to every configured SINK.

THE SINK LIST IS THE POINT OF THE STRUCTURE. Today there is one sink, which
prints to the console. A sink is any callable taking a parsed record, so
forwarding to a cloud endpoint, a database or a file is a new function and a
config entry -- not a change to anything here.

Usage:
    base-station.py                 set the clock, print records
    base-station.py --no-clock      monitor only, leave the clock alone
    base-station.py --config c.json add sinks from a config file
    base-station.py --raw           print lines as they arrive, unparsed

Config file (JSON), all keys optional:

    {
      "sinks": [
        {"type": "console"},
        {"type": "file", "path": "base.log"}
      ]
    }
"""

import argparse
import fcntl
import json
import os
import re
import sys
import time
from dataclasses import dataclass, field
from typing import Callable, Optional

try:
    import serial
except ImportError:
    sys.exit("pyserial is missing.  pip install pyserial")


# The base board's chip UID. Overridable for a different unit; the default is
# the one this project uses. See scripts/lib/common.sh, which holds the same
# value for the shell tools.
BASE_SERIAL = os.environ.get("PRORF_BASE_SERIAL", "B56628F85150484735202020FF082C1A")

BY_ID = "/dev/serial/by-id"


def find_port(serial_no: str) -> Optional[str]:
    """The by-id path for one specific board, or None.

    Deliberately narrow: it matches SAMD21_Pro_RF and the given UID and nothing
    else. Other boards on this machine belong to other projects, and opening a
    tty is not a passive act -- it asserts DTR and can reset whatever is on the
    far end.
    """
    if not os.path.isdir(BY_ID):
        return None
    for name in sorted(os.listdir(BY_ID)):
        if "SAMD21_Pro_RF" in name and serial_no in name:
            return os.path.join(BY_ID, name)
    return None


def lock_port(port: str):
    """Advisory lock, matching the shell scripts' convention.

    Two readers on one tty is not blocked by Linux and produces interleaved
    partial reads that look like corruption on the wire.
    """
    node = os.path.basename(os.path.realpath(port))
    fh = open(f"/tmp/prorf-{node}.lock", "w")
    try:
        fcntl.flock(fh, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        sys.exit(f"another process holds {port} -- close the monitor first")
    return fh


# ------------------------------------------------------------------ records --

# Lines look like:   R:05:57:01 0506:$XXWTH,1981.6,27.36,796.77,28.19*46
#                    ^kind ^stamp ^seq ^sentence            ^checksum
LINE = re.compile(
    r"^(?P<kind>[A-Z]):(?P<stamp>[0-9:.+]+)\s+"
    r"(?:(?P<seq>\d+):)?(?P<sentence>\$[^*]+)\*(?P<checksum>[0-9A-Fa-f]{2})\s*$"
)

KIND_NAMES = {
    "R": "received record",
    "Q": "link quality",
    "X": "foreign traffic",
    "H": "silence heartbeat",
    "I": "own reading",
}


@dataclass
class Record:
    kind: str
    stamp: str
    seq: Optional[int]
    sentence: str          # "$XXWTH,1981.6,..." with no checksum
    fields: list
    checksum_ok: bool
    raw: str

    @property
    def talker(self) -> str:
        return self.fields[0] if self.fields else ""


def nmea_checksum(sentence: str) -> int:
    """XOR of everything between '$' and '*'.

    Note it covers the SENTENCE ONLY. The sequence prefix in front of it is
    outside the checksum, which is why this is handed `sentence` and not the
    whole line.
    """
    body = sentence[1:] if sentence.startswith("$") else sentence
    ck = 0
    for ch in body:
        ck ^= ord(ch)
    return ck


def parse(line: str) -> Optional[Record]:
    m = LINE.match(line.strip())
    if not m:
        return None
    sentence = m.group("sentence")
    want = int(m.group("checksum"), 16)
    return Record(
        kind=m.group("kind"),
        stamp=m.group("stamp"),
        seq=int(m.group("seq")) if m.group("seq") else None,
        sentence=sentence,
        fields=sentence.lstrip("$").split(","),
        checksum_ok=(nmea_checksum(sentence) == want),
        raw=line.rstrip(),
    )


# -------------------------------------------------------------------- sinks --

Sink = Callable[[Record], None]


def console_sink() -> Sink:
    def emit(rec: Record) -> None:
        flag = "" if rec.checksum_ok else "  [BAD CHECKSUM]"
        print(f"{rec.raw}{flag}", flush=True)
    return emit


def file_sink(path: str) -> Sink:
    fh = open(path, "a", buffering=1)
    def emit(rec: Record) -> None:
        fh.write(rec.raw + "\n")
    return emit


SINK_TYPES = {"console": lambda cfg: console_sink(),
              "file": lambda cfg: file_sink(cfg["path"])}


def build_sinks(config: dict) -> list:
    specs = config.get("sinks") or [{"type": "console"}]
    sinks = []
    for spec in specs:
        kind = spec.get("type")
        if kind not in SINK_TYPES:
            sys.exit(f"unknown sink type {kind!r}; known: {', '.join(SINK_TYPES)}")
        sinks.append(SINK_TYPES[kind](spec))
    return sinks


# --------------------------------------------------------------------- main --

def set_clock(ser) -> None:
    """Send 'T<epoch>' -- the form lib/rtc::pollSerial() accepts.

    UTC, because every timestamp in this system is UTC and a log that mixes
    zones is worse than one with no timestamps at all.
    """
    epoch = int(time.time())
    ser.write(f"T{epoch}\n".encode())
    ser.flush()
    print(f"==> clock sent: {time.strftime('%Y-%m-%d %H:%M:%S', time.gmtime(epoch))} UTC",
          flush=True)


def main() -> int:
    ap = argparse.ArgumentParser(description="Base station: set the clock, then monitor.")
    ap.add_argument("--port", help="serial port (default: find the base board by UID)")
    ap.add_argument("--serial-no", default=BASE_SERIAL, help="chip UID to look for")
    ap.add_argument("--config", help="JSON config naming sinks")
    ap.add_argument("--no-clock", action="store_true", help="do not set the clock")
    ap.add_argument("--raw", action="store_true", help="print every line, parsed or not")
    args = ap.parse_args()

    port = args.port or find_port(args.serial_no)
    if not port:
        print(f"no Pro RF with serial {args.serial_no} under {BY_ID}", file=sys.stderr)
        print("attached now:", file=sys.stderr)
        for name in sorted(os.listdir(BY_ID)) if os.path.isdir(BY_ID) else []:
            print(f"    {name}", file=sys.stderr)
        return 1

    config = json.load(open(args.config)) if args.config else {}
    sinks = build_sinks(config)

    lock = lock_port(port)                       # noqa: F841 -- held until exit
    print(f"==> base station on {port}", flush=True)

    with serial.Serial(port, 115200, timeout=1) as ser:
        # Opening the port resets the board, so this waits for it to come back
        # before sending anything. Without the pause the clock line lands in a
        # buffer that setup() is about to discard.
        time.sleep(2.0)
        ser.reset_input_buffer()
        if not args.no_clock:
            set_clock(ser)

        partial = b""
        while True:
            chunk = ser.read(512)
            if not chunk:
                continue
            partial += chunk
            while b"\n" in partial:
                line, partial = partial.split(b"\n", 1)
                text = line.decode("utf-8", "replace").rstrip("\r")
                if not text:
                    continue
                # '#' lines are the board's own field legend, meant for a human
                # reading the port. They are not data and no sink wants them.
                if text.lstrip().startswith("#"):
                    if args.raw:
                        print(text, flush=True)
                    continue
                rec = parse(text)
                if rec is None:
                    if args.raw:
                        print(text, flush=True)
                    continue
                for sink in sinks:
                    sink(rec)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print()
        sys.exit(130)
