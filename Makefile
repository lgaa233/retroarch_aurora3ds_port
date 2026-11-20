# RetroArch for AuroraOS (Nintendo 3DS)
#
#   make            builds RetroArch.bin with the AuroraOS C SDK
#   make test       builds and runs the host-side core tests (plain gcc)
#   make clean      removes build output
#
# Needs: arm-none-eabi-gcc on PATH (devkitARM, or Ubuntu's gcc-arm-none-eabi
# plus the compat/ header this Makefile puts on CPATH), python3, git, and a
# host gcc for `make test`.

AURORA_DIR    ?= third_party/AuroraOS
AURORA_REPO   ?= https://github.com/TeamAuroraOS/AuroraOS.git
AURORA_COMMIT ?= 4506e82535d69f94a9bd1fd4708f26ada61cec51

OUT      := RetroArch.bin
AURCC    := $(AURORA_DIR)/sdk/aurcc.py
INCLUDES := -I $(CURDIR)/third_party/libretro \
            -I $(CURDIR)/third_party/peanut_gb \
            -I $(CURDIR)/third_party/agnes

SOURCES := $(wildcard src/*.c src/*.h src/cores/*.c src/cores/*.h) \
           src/RetroArch.icon

.PHONY: all test clean distclean aurora

all: $(OUT)

aurora: $(AURCC)

$(AURCC):
	git clone $(AURORA_REPO) $(AURORA_DIR)
	cd $(AURORA_DIR) && git checkout $(AURORA_COMMIT)

$(OUT): $(AURCC) $(SOURCES)
	CPATH=$(CURDIR)/compat python3 $(AURCC) build src -o $(OUT) $(INCLUDES)

# Host tests: the cores never touch the Aurora SDK, so they build and run on
# the PC with synthetic ROMs.
test: build/test_cores
	./build/test_cores

build/test_cores: tests/host/test_cores.c src/cores/cores.c \
                  src/cores/core_gb.c src/cores/core_nes.c src/cores/core.h
	@mkdir -p build
	$(CC) -O1 -g -Wall -Wextra -I third_party/libretro -I third_party/peanut_gb \
	  -I third_party/agnes -I src -o $@ tests/host/test_cores.c \
	  src/cores/cores.c src/cores/core_gb.c src/cores/core_nes.c

clean:
	rm -rf build $(OUT) $(AURORA_DIR)/sdk/build

distclean: clean
	rm -rf $(AURORA_DIR)
