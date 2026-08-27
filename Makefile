# CLI build/test/deploy for the SparkFun Pro RF - LoRa, 915MHz (SAMD21).
#
# PlatformIO does the work; this is a thin, discoverable front door so the
# common operations are one word and nobody has to remember pio's flags. It is
# also what CLion, cron, CI or another agent should call -- one entry point
# means the IDE and the terminal cannot drift apart.

PIO      ?= $(HOME)/.local/bin/pio
APP      ?= probe
# Which physical Pro RF. They are the same model with the same VID:PID; only
# the SAMD21's chip UID, exposed as the USB serial number, tells them apart.
BOARD    ?= sensor
# make variables are case-sensitive, so `make run app=node` would silently use
# the default instead of node. Accept the lowercase form rather than doing the
# wrong thing quietly.
ifdef app
APP := $(app)
endif
# Only a fallback for calling pio by hand. The scripts autodetect, because the
# board's by-id name changes between running a sketch and sitting in SAM-BA.
PORT     ?= /dev/serial/by-id/usb-SparkFun_SFE_SAMD21_Pro_RF_21A8043C5150484735202020FF09181F-if00

APPS     := sensor base
TOOLS    := probe i2c_scan qwiic_health qwiic_bench openlog_test radio_rx radio_tx radio_regs radio_sweep qwiic_read
TARGETS  := $(APPS) $(TOOLS)

ROOT     := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# ---------------------------------------------------------------- build flags
# ACCUMULATED, never assigned. Each knob used to do its own
# `export PLATFORMIO_BUILD_FLAGS := ...`, so setting two at once meant the last
# block silently discarded the others -- `DEV=ccs811 I2CHZ=100000` built the
# DEFAULT device at 100 kHz and cheerfully labelled its own output wrong.
BUILD_FLAGS :=

# TXPOWER=<dBm>, 2..20. PA_BOOST is the only output path, so 2 is the floor.
# PRORF_TX_POWER_SET marks it as explicitly requested, which is how a tool with
# a lower bench default tells that apart from board.h's default being visible.
ifdef TXPOWER
BUILD_FLAGS += -DPRORF_TX_POWER_DBM=$(TXPOWER) -DPRORF_TX_POWER_SET=1
endif

# I2CHZ=<hz> overrides the Qwiic bus speed.
ifdef I2CHZ
BUILD_FLAGS += -DQWIIC_I2C_HZ=$(I2CHZ)UL
endif




# SKIP=log builds without the OpenLog. It ACKs its address whether or not a
# usable card is in it, so this is how you find out whether a write blocks.
ifeq ($(SKIP),log)
BUILD_FLAGS += -DDATALOG_DISABLE
endif

# DEV=<name> selects which device qwiic_bench measures.
#
BENCH_bme280  := -DBENCH_ADDR=0x77 -DBENCH_NAME='"BME280"'  -DBENCH_REG=0xD0 -DBENCH_LEN=1
BENCH_ccs811  := -DBENCH_ADDR=0x5B -DBENCH_NAME='"CCS811"'  -DBENCH_REG=0x00 -DBENCH_LEN=1
BENCH_openlog := -DBENCH_ADDR=0x2A -DBENCH_NAME='"OpenLog"' -DBENCH_REG=0x00 -DBENCH_LEN=1
ifdef DEV
BUILD_FLAGS += $(BENCH_$(DEV))
endif

export PLATFORMIO_BUILD_FLAGS := $(BUILD_FLAGS)

# TXPOWER=n overrides the transmit power at build time, in dBm. PA_BOOST is the
# only output path on this module, so the usable range is 2..20 and 2 is the
# floor. Use it on a bench: two of these at 30 cm with full-size antennas
# overload the receiver at the 14 dBm deployment setting.
#
#     make run-sensor TXPOWER=2
#
# PlatformIO appends PLATFORMIO_BUILD_FLAGS to build_flags from the ini.
#
# I2CHZ=<hz> overrides the Qwiic bus speed, which is how the OpenLog's 400 kHz
# limit was found:  make flash-openlog_test I2CHZ=50000
.DEFAULT_GOAL := help
.PHONY: help build $(TARGETS) flash monitor monitor-base watch-base run watch capture test check board dump clock usb udev ide clean distclean
# NOT .PHONY for run-% / flash-%. GNU make skips implicit and pattern rule
# lookup for targets declared phony, so listing them there stops flash-% from
# matching at all and `make flash-radio_tx` answers "Nothing to be done" while
# `make -p` cheerfully shows the rule and its recipe. No file of that name
# exists, so the pattern rule runs every time without help.

help:
	@echo 'SparkFun Pro RF - LoRa, 915MHz (SAMD21)'
	@echo
	@echo '  applications : $(APPS)'
	@echo '  tools        : $(TOOLS)'
	@echo '  default APP  : $(APP)              override with APP=<name>'
	@echo
	@echo '  make build              build every program'
	@echo '  make <name>             build one (e.g. make radio_rx)'
	@echo '  make run-<name>         build, upload, then watch it  <- the usual one'
	@echo '  make flash-<name>       build and upload'
	@echo '                          (make run APP=<name> also works)'
	@echo '  make monitor            serial monitor only (Ctrl-C to quit)'
	@echo '  make monitor-base       watch the base station receive'
	@echo '  make watch-base         same, but survives a RESET at either end'
	@echo
	@echo '  BOARD=sensor|base       which physical Pro RF (default: sensor)'
	@echo '                          sensor = the sensor board, base = the receiver'
	@echo '  make watch              like monitor, but survives a RESET'
	@echo '                          (both forward keystrokes to the board)'
	@echo '  make board              identify the attached board end to end'
	@echo '  make dump               read the firmware off the board'
	@echo '  make test               host-side unit tests, no board needed'
	@echo '  make check              build everything + tests + shellcheck'
	@echo '  make ide                regenerate the CLion CMake project'
	@echo '  make udev               install PlatformIO udev rules (needs sudo)'
	@echo
	@echo '  TXPOWER=<dBm>           transmit power, 2..20. Bench: TXPOWER=2'
	@echo '  SKIP=log                build without the OpenLog'
	@echo '  make clock              set the board clock from this desktop'
	@echo '  make usb                watch a board plug in and diagnose it'
	@echo '  make clean / distclean'
	@echo
	@echo '  sensor and radio_tx transmit; everything else receives only.'

# Build every program. Deliberately NOT `pio run` with no -e: that would also
# try to build `native`, which has no src and no main().
build:
	$(PIO) run $(addprefix -e ,$(TARGETS))

$(TARGETS):
	$(PIO) run -e $@

# scripts/flash.sh rather than raw pio: it finds the port itself. The board
# presents a different by-id name in its bootloader (no USB serial number), so
# anything with a hardcoded path fails exactly when you need it most.
# `make run-node` reads better than `make run APP=node`, and make has no
# positional arguments -- `make run node` would ask make to build two separate
# targets, `run` and `node`, which is not what anyone means by it. Pattern
# rules give the natural spelling.
flash-%:
	@BOARD=$(BOARD) $(ROOT)scripts/flash.sh $*

run-%:
	@BOARD=$(BOARD) $(ROOT)scripts/flash.sh $* --monitor

flash:
	@BOARD=$(BOARD) $(ROOT)scripts/flash.sh $(APP)

# Build, upload, then monitor. Keystrokes reach the board, so `probe` and
# `i2c_scan` re-run on any key.
run:
	@BOARD=$(BOARD) $(ROOT)scripts/flash.sh $(APP) --monitor

monitor:
	@BOARD=$(BOARD) $(ROOT)scripts/monitor.sh

# Spelled out because it is the common case and `BOARD=base make monitor` is
# easy to get subtly wrong -- `make monitor BOARD=base` also works, but putting
# the variable first is the habit the rest of these docs teach.
monitor-base:
	@BOARD=base $(ROOT)scripts/monitor.sh

# Like monitor-base, but survives a RESET at either end and keeps one
# timestamped transcript across the gap -- which is what you want while the
# sensor board is outdoors and may be power-cycled.
watch-base:
	@BOARD=base $(ROOT)scripts/capture.sh forever

# Like monitor, but survives RESET: the tty vanishes and comes back, and this
# reconnects and keeps one timestamped transcript across the gap. Keystrokes
# are forwarded, so probe and i2c_scan still re-run on a keypress.
watch:
	@BOARD=$(BOARD) $(ROOT)scripts/capture.sh forever

capture: watch

board:
	@BOARD=$(BOARD) $(ROOT)scripts/board-check.sh

dump:
	@$(ROOT)scripts/dump-flash.sh

test:
	$(PIO) test -e native

# What CI would run. Everything that does not need the board attached.
check: build test
# The `if` matters. This was written as
#
#     command -v shellcheck && shellcheck ... && echo clean || echo skipped
#
# where a shellcheck FAILURE short-circuits into the || and prints "not
# installed, skipped" -- reporting a missing tool, and exiting 0. `make check`
# could not fail on a shellcheck error, which is the one thing it is for.
	@if command -v shellcheck >/dev/null 2>&1; then \
	    shellcheck -x $(ROOT)scripts/*.sh $(ROOT)scripts/lib/common.sh \
	      && echo 'shellcheck: clean'; \
	else \
	    echo 'shellcheck: NOT INSTALLED -- scripts were NOT checked'; \
	fi

# CLion drives C/C++ from CMake, and PlatformIO generates it. The result is for
# INDEXING ONLY -- building through CMake does not produce a flashable image.
# Re-run after adding an environment or CLion will not see the new program.
# Needs root, so it prompts. Removes PlatformIO's udev warning and stops
# ModemManager probing a board mid-upload.
clock:
	@BOARD=$(BOARD) $(ROOT)scripts/set-clock.sh

# Watch the kernel log while a board is plugged in, and classify what happened.
usb:
	@$(ROOT)scripts/usb-watch.sh

udev:
	@$(ROOT)scripts/install-udev-rules.sh

ide:
	$(PIO) project init --ide clion
	@echo 'CMake project regenerated. Build with make/pio, not with CMake.'

clean:
	$(PIO) run -t clean $(addprefix -e ,$(TARGETS))

distclean:
	rm -rf $(ROOT).pio
