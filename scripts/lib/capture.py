"""Serial capture that survives a reset, and forwards keystrokes.

Kept in its own file rather than a heredoc inside capture.sh, and that is not
a style preference. `python - <<'PY'` feeds the script to the interpreter on
stdin, so sys.stdin IS the script source: it is not a tty, it has already been
consumed, and nothing typed at the terminal can ever reach it. Any keystroke
forwarding written that way is dead code that looks correct.

Usage:  capture.py <port> <seconds|0 for forever> [--plain]

--plain drops the elapsed-time column and the connect/disconnect markers, for
use as a straight serial monitor. Everything else is identical -- notably the
cbreak terminal mode, which is why Ctrl-C works here and did not under
miniterm: see the setcbreak call below.
"""

import os
import select
import signal
import sys
import time

import serial


def main() -> int:
    # A plain `kill` must still run the finally block below that restores the
    # terminal. SIGTERM's default action terminates the process outright, so
    # finally never runs and the terminal is left in cbreak with whatever
    # flags it had -- which is exactly how a terminal in this project ended up
    # permanently unable to generate SIGINT.
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(143))

    port = sys.argv[1]
    duration = float(sys.argv[2])
    plain = "--plain" in sys.argv[3:]
    if duration <= 0:
        duration = float("inf")

    # Forward keystrokes to the board. Without cbreak the terminal
    # line-buffers, so nothing reaches the serial port until Enter and then it
    # arrives as a whole line -- not what a sketch polling Serial.available()
    # expects.
    #
    # cbreak AND NOT setraw, and the difference is the whole reason this
    # script now backs `make monitor` too. cbreak leaves ISIG enabled, so the
    # KERNEL still turns Ctrl-C into SIGINT no matter what this program is
    # doing. setraw disables ISIG: Ctrl-C becomes an ordinary byte that the
    # application has to notice and act on, and if its input path is busy,
    # blocked, or simply compares the wrong thing, there is no key left that
    # can end the session. miniterm uses setraw, which is how `make monitor`
    # ended up unquittable by Ctrl-C or Ctrl-] -- twice.
    raw = None
    if sys.stdin.isatty():
        try:
            import termios
            import tty

            raw = termios.tcgetattr(sys.stdin)
            tty.setcbreak(sys.stdin.fileno())

            # EXPLICITLY RE-ENABLE ISIG. setcbreak does NOT set it -- read
            # tty.cfmakecbreak and it is one line, `LFLAG &= ~(ECHO|ICANON)`.
            # It only CLEARS flags. So cbreak preserves whatever ISIG state
            # the terminal arrived in, and a terminal that arrives with ISIG
            # off keeps it off.
            #
            # That is not hypothetical: a previous program in this project
            # left a terminal in raw mode when it was killed, every later
            # program inherited ISIG off, and Ctrl-C stopped generating SIGINT
            # in that terminal for good. The monitor looked unquittable and
            # the monitor was not at fault.
            #
            # Setting it here means this program does not care what state the
            # terminal was handed to it in.
            mode = termios.tcgetattr(sys.stdin)
            mode[3] |= termios.ISIG          # index 3 is lflag
            termios.tcsetattr(sys.stdin, termios.TCSANOW, mode)
        except Exception:
            raw = None

    start = time.time()

    def stamp() -> str:
        return f"{time.time() - start:7.2f}"

    def emit(prefix: str, text: str) -> None:
        if plain:
            # A marker line carries no board output, so in plain mode it is
            # noise rather than information.
            if prefix != "|":
                return
            print(text, flush=True)
        else:
            print(f"{stamp()}  {prefix} {text}", flush=True)

    connected = False
    partial = b""
    ser = None

    try:
        while time.time() - start < duration:
            if ser is None:
                # Tight poll. The device node reappears a second or two after
                # a reset and every millisecond of delay here is output lost.
                if not os.path.exists(port):
                    time.sleep(0.005)
                    continue
                try:
                    ser = serial.Serial(port, 115200, timeout=0.05)
                    ser.dtr = True
                except Exception:
                    ser = None
                    time.sleep(0.02)
                    continue
                if not connected:
                    connected = True
                    emit("--", "[port opened]")
                else:
                    emit("--", "[port reappeared -- the board rebooted]")
                continue

            if sys.stdin.isatty():
                try:
                    readable, _, _ = select.select([sys.stdin], [], [], 0)
                    if readable:
                        key = os.read(sys.stdin.fileno(), 64)
                        if key:
                            ser.write(key)
                            ser.flush()
                except Exception:
                    pass

            try:
                chunk = ser.read(512)
            except Exception as exc:
                emit("--", f"[port lost: {type(exc).__name__} -- this is what RESET looks like]")
                try:
                    ser.close()
                except Exception:
                    pass
                ser = None
                continue

            if not os.path.exists(port):
                emit("--", "[port disappeared -- the board is resetting]")
                try:
                    ser.close()
                except Exception:
                    pass
                ser = None
                continue

            if not chunk:
                continue

            partial += chunk
            while b"\n" in partial:
                line, partial = partial.split(b"\n", 1)
                emit("|", line.decode("utf-8", "replace").rstrip("\r"))

    except KeyboardInterrupt:
        print(flush=True)
    finally:
        if raw is not None:
            import termios

            # Restore what we were given -- and then FORCE the three flags that
            # decide whether a shell is usable at all.
            #
            # Restoring the saved state alone is not enough, and that is what
            # bit the repository owner a second time. If this program is handed
            # a terminal that a previous program already broke, `raw` IS the
            # broken state, and putting it back faithfully reinstates the
            # breakage: no echo, no line editing, no Ctrl-C. The terminal was
            # left unusable AFTER the monitor had exited.
            #
            # A serial monitor has no business leaving a shell in that state,
            # whatever it inherited. ECHO and ICANON give back typing and line
            # editing; ISIG gives back Ctrl-C.
            mode = list(raw)
            mode[3] |= termios.ECHO | termios.ICANON | termios.ISIG
            termios.tcsetattr(sys.stdin, termios.TCSADRAIN, mode)

    if partial:
        emit("|", partial.decode("utf-8", "replace") + "   [no newline -- output stopped mid-line]")
    emit("--", "[capture ended]")
    if ser:
        try:
            ser.close()
        except Exception:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
