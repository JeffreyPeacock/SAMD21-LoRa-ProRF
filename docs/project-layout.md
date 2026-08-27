# Project layout

How this repository is organised to hold many programs — a few large, most
tiny — and how to build, test and deploy them from the command line and from
CLion.

## The shape

```
SAMD21-LoRa-ProRF/
├── platformio.ini          one [env:*] per buildable program
├── Makefile                the CLI front door: make help
├── CMakeLists.txt          generated for CLion; indexing only, NOT for building
│
├── lib/                    OUR shared libraries -- committed
│   ├── board/board.h                the pin map
│   ├── nmea/nmea.h                  float formatting, NMEA checksums
│   ├── telemetry/telemetry.h        the wire format
│   ├── rfm95/rfm95.{h,cpp}          RFM95W bring-up
│   ├── datalog/datalog.{h,cpp}      Qwiic OpenLog, made optional
│   └── qwiic/qwiic.{h,cpp}          the Qwiic chain behind one API
│
├── src/
│   ├── apps/               full applications, one directory each
│   │   ├── node/main.cpp
│   │   └── base/main.cpp
│   └── tools/              small single-purpose programs
│       ├── probe/main.cpp
│       ├── i2c_scan/main.cpp
│       ├── radio_rx/main.cpp
│       ├── radio_tx/main.cpp
│       └── qwiic_read/main.cpp
│
├── test/                   Unity tests, run on the HOST
│   ├── test_nmea/test_nmea.cpp
│   └── test_telemetry/test_telemetry.cpp
│
├── scripts/                board-check, flash, monitor, capture, dump-flash
├── docs/
└── etc/                    reference material, firmware dumps -- gitignored
```

**This mirrors the sibling `../ESP32-LoRa-1Ch-Gateway` project deliberately.**
Same `src/apps/` + `src/tools/` split, same committed `lib/`, same
`make`-over-`pio` front door. Two boards that talk to each other are easier to
work on when moving between their repositories costs nothing.

## Why shared code lives in `lib/`, not `src/`

`lib/` is PlatformIO's private-library directory, and it is not merely a
convention — it changes three things:

**Include paths become absolute rather than relative.** A component at
`lib/board/board.h` is included as:

```c
#include <board.h>                // not "../board.h", and not "../../board.h"
```

PlatformIO puts each library's directory on the compiler's search path. That
is why component names are distinctive — `nmea`, `telemetry`, `rfm95`,
`datalog`, `qwiic` — rather than generic. A `lib/text/text.h` or
`lib/proto/proto.h` is asking to collide with a vendor header, and the
collision would be silent.

**Only what is used gets compiled.** PlatformIO's Library Dependency Finder
scans each program's `#include`s and links just those components. A one-file
tool that includes only `<prorf/board.h>` does not drag in the sensor stack.
Nothing has to be listed per environment for this to work.

**Components become independently testable.** A `lib/` library can be compiled
for the host, which is what makes the `native` test environment below possible.

The cost is one extra directory level per component. For an old C/C++ hand this
is the familiar `include/` + `src/` split, applied per component rather than
per project.

## Why every program gets its own directory under `src/`

PlatformIO builds *everything* under `src/` by default, and two `main()`s will
not link. Each environment therefore selects exactly one directory:

```ini
[env:node]
build_src_filter = +<apps/node/>

[env:i2c_scan]
build_src_filter = +<tools/i2c_scan/>
```

The rule that follows: **one directory per program, and no loose `.cpp` files
directly in `src/`, `src/apps/` or `src/tools/`.** A stray file at one of those
levels is either compiled into every program or into none, and which one
depends on filter syntax nobody remembers.

`src/tools/` holds the small ones. They are not lesser programs — `probe` is
the first thing to run on a suspect board — they are just single-purpose and
usually a single file.

## Environments, and keeping platformio.ini short

Every program needs an `[env:*]` block, and with a dozen tools that is a lot of
repetition. `extends` removes it:

```ini
[env]                       ; applies to EVERY environment, native included
build_flags = -Wall -Wextra

[samd21]                    ; a plain section: never built, only referenced
platform  = atmelsam
board     = sparkfun_samd21_proRF
framework = arduino
monitor_speed = 115200

[env:i2c_scan]
extends = samd21
build_src_filter = +<tools/i2c_scan/>
```

**The board settings must NOT live in `[env]`.** PlatformIO applies `[env]` to
every environment, and the `native` test platform rejects a board ID with
"Unknown board ID". This is why there is a separate `[samd21]` section that
board environments `extends` — the same trap the sibling ESP32 gateway project
documents from the other side.

`${section.key}` interpolation works alongside it, and `[deps]` uses it to
group pinned library sets so `lib_deps` is written once per group rather than
once per environment.

**Name environments after the program**, not after the role. `pio run -e
qwiic_read` should be unambiguous without consulting a table.

## Tests: host tests are the ones worth having

PlatformIO's `test/` directory holds Unity tests, and each `test_*` subdirectory
is a separate test program. The important split is where they run.

**On the host (`platform = native`)** — no board, no upload, runs in
milliseconds:

```ini
[env:native]
platform = native
test_framework = unity
build_flags = -std=gnu++17 -DUNIT_TEST
```

Everything that is pure computation belongs here: `nmea.h`'s float formatting
and checksums, `telemetry.h`'s framing and length arithmetic. These are exactly
the functions where an off-by-one is invisible on an oscilloscope and obvious
in a unit test. Keeping them free of `Arduino.h` is what makes it possible, and
is a good discipline in its own right.

The `native` environment also needs `build_src_filter = -<*>`: everything under
`src/` is Arduino firmware and includes `<Arduino.h>`, which does not exist on
the host. `pio test` supplies its own `main()` from the Unity runner, so that
environment needs no `src/` at all.

Two things these tests already caught, which is the argument for having them:
a `sizeof()` of a 256-byte buffer silently truncating to 0 when passed as a
`uint8_t` capacity, and `nmea.h` using `strtol` without including
`<stdlib.h>` — invisible on Arduino, where the core pulls it in transitively.

**On the board** — for anything touching hardware: an I2C scan that asserts the
expected addresses answered, a radio loopback. Slower, needs the board
attached, but the only way to test the parts that matter physically.

```bash
pio test -e native            # host tests, no hardware
pio test -e node              # on-target tests, needs the board
```

## The command line is the build system

CLion is an editor over this, never the source of truth. Everything must work
from a terminal and from CI:

```bash
make                          # what you can do
make build                    # every program
make radio_rx                 # one program
make flash APP=probe          # build and upload
make run APP=probe            # flash, then monitor
make capture                  # serial capture that survives a RESET
make board                    # identify the attached board end to end
make dump                     # read the firmware off the board
make test                     # host tests, no board needed
make check                    # build everything + tests + shellcheck
make ide                      # regenerate the CLion CMake project
make clean / distclean
```

`make build` deliberately names its environments rather than running a bare
`pio run`, which would also try to build `native` — an environment with no
`src/` and no `main()`.

The `Makefile` is a wrapper over `pio`, not a second build system — every
target is one `pio` invocation. Old habits (`make`, `make test`) work, and so
does `pio run -e sensor -t upload` for anyone who prefers it.

`scripts/` holds what `pio` cannot do: `board-check.sh` (is the host, cable,
port and board actually working), `capture.sh` (serial capture that survives a
reset), `dump-flash.sh` (read firmware off the board), `flash.sh`,
`monitor.sh`.

## CLion

CLion drives C/C++ projects from CMake, and PlatformIO can generate that:

```bash
pio project init --ide clion
```

This writes `CMakeLists.txt` and `CMakeListsPrivate.txt`. Three things to know:

**The CMake project is for indexing and navigation, not for building.** It
exists so CLion can resolve `#include <prorf/board.h>`, offer completion, and
jump to definitions. Building through CMake does not produce a flashable image.
Build with `make` / `pio`, or install the **PlatformIO for CLion** plugin,
which adds proper build/upload/monitor run configurations.

**Both `CMakeLists.txt` and `CMakeListsPrivate.txt` are gitignored.** They are
derived files holding absolute paths into `~/.platformio`; regenerate them,
never edit them, never commit them.

**Re-run after adding an environment**, or CLion will not see the new program.
`make ide` does this.

A caveat specific to this setup: CLion will index every environment's sources
at once, so a symbol defined differently in two programs can look ambiguous in
the editor while compiling correctly. That is an artefact of the generated
CMake, not a real conflict.

## Conventions

- **One definition of the pin map**, `lib/board/board.h`. Nothing else declares
  a pin. This is the single most important rule here — see `docs/hardware.md`
  for what happens when a pin map is guessed.
- **Distinctive component names**, so `<nmea.h>` and `<telemetry.h>` cannot
  collide with a vendor header. Never `text.h`, `proto.h`, `util.h`.
- **No `using namespace` in a header.**
- **Pure logic stays free of `Arduino.h`** so it can be host-tested.
- **A program under `src/` is a `main.cpp` and as little else as possible.**
  Anything worth reusing moves to `lib/`, and anything in `lib/` gets a test.
- **Third-party libraries are never committed.** `platformio.ini` pins them by
  version and PlatformIO fetches them to `.pio/libdeps/`. See
  `docs/qwiic-bus.md` for why the pinning is not optional.
