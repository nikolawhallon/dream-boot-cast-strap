# Build/run this game for Linux and for the Dreamcast. Run inside `nix-shell`.
# `make` (or `make help`) lists the targets.
#
# Everything is delegated to CMake (see CMakeLists.txt). This file just picks the
# right build directory, Simulant build and toolchain for each target.

# Disc title (plain ASCII)
TITLE ?= DreamGame
# Release or Debug
TYPE ?= Release
# 1 = pad the disc's data track: faster loading on real hardware, but a ~700MB image
PAD ?= 0
# Screenshots: seconds after launch (empty = the script's default: 5 on PC, 15 on Dreamcast,
# which includes ~8 s of booting)
WAIT ?=

type        := $(shell echo $(TYPE) | tr A-Z a-z)
SIMULANT_PC := $(DC_SDK)/build/simulant-linux-$(type)
SIMULANT_DC := $(DC_SDK)/build/simulant-dreamcast-$(type)
BUILD_PC    := build/linux-$(type)
BUILD_DC    := build/dreamcast-$(type)
DISC_FLAGS  := $(if $(filter 1,$(PAD)),,--disable-data-track-padding)

.PHONY: help check-shell check-dc pc run-pc dc cdi run-dc run-dc-elf shot-pc shot-dc clean

help:
	@echo "Linux:"
	@echo "  make pc          build                 -> $(BUILD_PC)/game"
	@echo "  make run-pc      build + run"
	@echo "Dreamcast:"
	@echo "  make dc          cross-compile         -> $(BUILD_DC)/game.elf"
	@echo "  make cdi         bootable disc image   -> $(BUILD_DC)/game.cdi"
	@echo "  make run-dc      build disc + boot it in Flycast"
	@echo "  make run-dc-elf  boot game.elf directly (no disc: /cd/assets won't exist)"
	@echo "Headless screenshots (no window, software GL):"
	@echo "  make shot-pc / make shot-dc   -> build/screenshot-{pc,dc}.png (+ .log)"
	@echo "  make shot-dc WAIT=30          -> the same, 30 s after launch"
	@echo "Other:"
	@echo "  make clean       delete build/ (do this after rebuilding KOS or Simulant)"
	@echo "Options: TYPE=Debug|Release  PAD=0|1  TITLE=...  WAIT=<seconds>"

check-shell:
	@if [ -z "$$DC_KIT" ]; then echo "Run 'nix-shell' first." >&2; exit 1; fi

check-dc: check-shell
	@if [ -z "$$KOS_BASE" ]; then echo "Dreamcast SDK not built/loaded: run dc-bootstrap.sh, then re-enter nix-shell." >&2; exit 1; fi

# ---- Simulant engine: built once per target/type, shared by all projects using the kit

$(SIMULANT_PC)/simulant/libsimulant.so:
	dc-build-simulant.sh linux $(TYPE)

$(SIMULANT_DC)/simulant/libsimulant.a:
	dc-build-simulant.sh dreamcast $(TYPE)

# ---- Linux

pc: check-shell $(SIMULANT_PC)/simulant/libsimulant.so
	cmake -S . -B $(BUILD_PC) -G Ninja -DCMAKE_BUILD_TYPE=$(TYPE) \
	      -DSIMULANT_SOURCE_DIR=$(DC_SDK)/simulant -DSIMULANT_BUILD_DIR=$(SIMULANT_PC)
	cmake --build $(BUILD_PC)

run-pc: pc
	cd $(BUILD_PC) && ./game

# ---- Dreamcast

dc: check-dc $(SIMULANT_DC)/simulant/libsimulant.a
	cmake -S . -B $(BUILD_DC) -G Ninja -DCMAKE_BUILD_TYPE=$(TYPE) \
	      -DSIMULANT_SOURCE_DIR=$(DC_SDK)/simulant -DSIMULANT_BUILD_DIR=$(SIMULANT_DC) \
	      -DCMAKE_TOOLCHAIN_FILE=$(DC_SDK)/simulant/toolchains/Dreamcast.cmake
	cmake --build $(BUILD_DC)

# Disc layout: game.elf becomes the boot file (1ST_READ.BIN); assets/ lands at /cd/assets
cdi: dc
	rm -f $(BUILD_DC)/game.cdi
	mkdcdisc --no-banner $(DISC_FLAGS) -e $(BUILD_DC)/game.elf -d $(BUILD_DC)/assets \
	         -n "$(TITLE)" -o $(BUILD_DC)/game.cdi
	@echo "Disc image: $(BUILD_DC)/game.cdi"

run-dc: cdi
	flycast $(BUILD_DC)/game.cdi

run-dc-elf: dc
	flycast $(BUILD_DC)/game.elf

# ---- Screenshots

shot-pc: pc
	TYPE=$(type) dc-screenshot.sh pc build/screenshot-pc.png $(WAIT)

shot-dc: cdi
	TYPE=$(type) dc-screenshot.sh dc build/screenshot-dc.png $(WAIT)

clean:
	rm -rf build
