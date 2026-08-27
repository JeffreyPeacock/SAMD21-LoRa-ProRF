# Verify the Pro RF end to end

Confirm the host, the serial path and the board are all working, and report what
firmware is running. Run this before believing any negative result.

Requires the board attached over USB micro-B with a **data** cable.

```bash
./scripts/board-check.sh
```

Interpret the output as follows, and say which case applies rather than
reporting "the board is dead":

| Symptom | What it means |
|---|---|
| nothing on the USB bus at all | almost always a charge-only cable. Try another before anything else. |
| enumerates with **no** `ID_SERIAL_SHORT` and `ID_VENDOR=Arduino_LLC` | the board is in its SAM-BA bootloader. Not a fault — this is the state uploads need. The PID stays `214f`, so do not look for `215f`. |
| enumerates as `1b4f:214f`, prints nothing | a sketch is running that either does not print or hung in `setup()` before its first output. Upload `probe`. |
| port exists but is not writable | not in the `dialout` group. `sudo usermod -aG dialout $USER`, then log out and in. |
| `port is free` warning names a process | something else has the tty. Two processes on one serial port produce confusing partial reads. |

If the board prints nothing, the next step is:

```bash
./scripts/flash.sh probe --monitor
```

`probe` scans the Qwiic bus and reads the SX1276's `RegVersion`. It answers
"is the radio there" and "what is on the I2C chain" without transmitting, so it
is safe with no antenna attached.

If the upload itself fails, the board's USB stack is not answering the
1200-baud touch — tell the user to **double-tap RESET** and try again. Do not
conclude the board is broken until that has been tried.
