# Enter planning mode for a task

The task to plan is the argument text passed to this command. If no argument was given, ask what to
plan before proceeding.

## Step 1 — Enter plan mode

Use ToolSearch with query `"select:EnterPlanMode"` to load the tool schema, then call EnterPlanMode.

## Step 2 — Follow the plan mode workflow

Once in plan mode, follow the phased workflow from the plan mode system instructions: understand,
design, review with the user, write the plan file, then call ExitPlanMode for approval.

Treat the argument as the user's planning request — as if they had typed it while already in plan
mode.

## Project note

**Most of the uncertainty here is physical, and the code cannot answer it.** This project's expensive
surprises have almost all been hardware behaving unlike its documentation:

- a library whose `softReset()` contains two unbounded loops and hangs `setup()`
- a logger that ACKs its address at 400 kHz and writes nothing
- a board that dies during USB enumeration with the cable that works on its twin
- devices that leave the I2C bus mid-session and return after a power cycle

A plan that assumes those away is worthless. Where a step depends on an unknown of that kind, say so
and give the cheapest way to find out — **usually a bench measurement, not more reading.** This
project has a tool for most of them: `probe`, `i2c_scan`, `qwiic_health`, `qwiic_bench`,
`radio_regs`, `qwiic_health`, `openlog_test`.

Sequence so that anything verifiable with **one board on the bench** happens first. Two-board link
work, and anything needing the ESP32 gateway — which belongs to another developer and whose port is
off limits — comes after.

**Prefer measuring to reasoning.** Several confident chains of reasoning in this project have been
wrong, and each was settled in minutes by an instrument. If a plan step contains the word "should",
ask what would show it.
