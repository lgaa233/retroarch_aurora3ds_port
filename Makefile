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
INCLUDES := -I $(CURDIR)/src \
            -I $(CURDIR)/third_party/libretro \
            -I $(CURDIR)/third_party/peanut_gb \
            -I $(CURDIR)/third_party/agnes \
            -I $(CURDIR)/third_party/smsplus \
            -I $(CURDIR)/third_party/minigb_apu \
            -I $(CURDIR)/third_party/nes_apu \
            -I $(CURDIR)/arm11 \
            -I $(CURDIR)/build
# The same flags parse for gcc and for aurcc.py's -D option.
DEFINES  := -DNOZIP_SUPPORT -DMAXIM_PSG -DUSE_Z80 \
            -DMINIGB_APU_AUDIO_FORMAT_S16SYS

SMSPLUS_SRC := $(wildcard third_party/smsplus/*.c)

SOURCES := $(wildcard src/*.c src/*.h src/cores/*.c src/cores/*.h) \
           $(SMSPLUS_SRC) $(wildcard third_party/smsplus/*.h) \
           third_party/minigb_apu/minigb_apu.c third_party/minigb_apu/minigb_apu.h \
           third_party/nes_apu/ra_nes_apu.c third_party/nes_apu/ra_nes_apu.h \
           arm11/ra_blit.c arm11/ra_blit.h arm11/blit11.h \
           src/RetroArch.icon

# The scaler as an ARM11 job (arm11/), which AuroraOS's core runs for the app
# (AUDIO_CMD_RUN11). Its own image, linked where src/video.c puts it.
ARM11_CC     ?= arm-none-eabi-gcc
ARM11_OBJCOPY ?= arm-none-eabi-objcopy
ARM11_CFLAGS := -march=armv6k -mtune=mpcore -marm -mfloat-abi=soft -O2 \
                -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns \
                -mno-unaligned-access -ffunction-sections -Wall -Wextra
ARM11_SRC    := arm11/blit11.c arm11/ra_blit.c

.PHONY: all test clean distclean aurora

all: $(OUT)

aurora: $(AURCC)

$(AURCC):
	git clone $(AURORA_REPO) $(AURORA_DIR)
	cd $(AURORA_DIR) && git checkout $(AURORA_COMMIT)

build/blit11.elf: $(ARM11_SRC) arm11/ra_blit.h arm11/blit11.ld
	@mkdir -p build
	$(ARM11_CC) $(ARM11_CFLAGS) -nostdlib -nostartfiles -T arm11/blit11.ld \
	  -Wl,--gc-sections -Wl,--build-id=none -o $@ $(ARM11_SRC)

build/blit11_blob.h: build/blit11.elf tools/bin2h.py
	$(ARM11_OBJCOPY) -O binary $< build/blit11.bin
	python3 tools/bin2h.py blit11_blob build/blit11.bin $@

$(OUT): $(AURCC) $(SOURCES) build/blit11_blob.h
	CPATH=$(CURDIR)/compat python3 $(AURCC) build src third_party/smsplus \
	  third_party/minigb_apu third_party/nes_apu -o $(OUT) $(INCLUDES) $(DEFINES)

# Host tests: the cores and the scaler never touch the Aurora SDK, so they
# build and run on the PC with synthetic ROMs and frames.
test: build/test_cores build/test_blit build/test_blit9 build/test_audio
	./build/test_cores
	./build/test_blit
	./build/test_blit9
	./build/test_audio

build/test_audio: tests/host/test_audio.c src/audio.c src/retroarch.h \
                  tests/host/fake/aurora_app.h
	@mkdir -p build
	$(CC) -O1 -g -Wall -Wextra -I tests/host/fake -I src \
	  -I third_party/libretro -o $@ tests/host/test_audio.c -lm

build/test_blit: tests/host/test_blit.c arm11/ra_blit.c arm11/ra_blit.h
	@mkdir -p build
	$(CC) -O1 -g -Wall -Wextra -I arm11 -o $@ tests/host/test_blit.c

build/test_blit9: tests/host/test_blit.c arm11/ra_blit.c arm11/ra_blit.h
	@mkdir -p build
	$(CC) -O1 -g -Wall -Wextra -DRA_BLIT_FULL_EXPAND=0 -I arm11 -o $@ \
	  tests/host/test_blit.c

TEST_SRC := tests/host/test_cores.c src/cores/cores.c src/cores/core_gb.c \
            src/cores/core_nes.c src/cores/core_sms.c src/cores/core_chip8.c \
            third_party/minigb_apu/minigb_apu.c \
            third_party/nes_apu/ra_nes_apu.c $(SMSPLUS_SRC)

build/test_cores: $(TEST_SRC) src/cores/core.h
	@mkdir -p build
	$(CC) -O1 -g -Wall -Wextra -Wno-unused-parameter $(DEFINES) \
	  -I third_party/libretro -I third_party/peanut_gb -I third_party/agnes \
	  -I third_party/smsplus -I third_party/minigb_apu \
	  -I third_party/nes_apu -I src \
	  -o $@ $(TEST_SRC) -lm

clean:
	rm -rf build $(OUT) $(AURORA_DIR)/sdk/build

distclean: clean
	rm -rf $(AURORA_DIR)
