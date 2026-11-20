# Building from Source

The repository already contains a prebuilt `dist/RetroArch.bin`. You only
need this page to rebuild it. `make` produces `RetroArch.bin` in the
repository root. It does not replace `dist/RetroArch.bin` by itself; copy
the new file to the SD card (and to `dist/` if you are updating the
prebuilt) when you want to install it. Install steps are on
[Installation](Installation.md).

The frontend is GPL-3.0. Cores keep their own licenses. See
[License](License.md).

## What the 3DS build needs

* `git`, to clone AuroraOS
* `python3`, to run Aurora's `sdk/aurcc.py`
* `arm-none-eabi-gcc` on `PATH`, either:
  * [devkitARM](https://devkitpro.org/wiki/Getting_Started), or
  * the Ubuntu / Debian packages `gcc-arm-none-eabi` and
    `libnewlib-arm-none-eabi`

The Debian newlib toolchain has no `<dirent.h>`. This repository's
`compat/dirent.h` is a devkitARM-style header, and the Makefile puts
`compat/` on `CPATH` when it invokes `aurcc.py`, so that toolchain can
build the SDK.

`make test` is separate. It uses the host `gcc` (or `cc`) and libm. It
does not need the ARM toolchain or a 3DS.

## Build

From the repository root:

```text
make
```

What that does:

1. If `third_party/AuroraOS/sdk/aurcc.py` is missing, `git clone` AuroraOS
   into `third_party/AuroraOS` and check out the pinned commit
   `4506e82535d69f94a9bd1fd4708f26ada61cec51`.
2. Run `aurcc.py build` on `src/` and `third_party/smsplus/`.
3. Write `RetroArch.bin` in the repository root.

The pinned commit is the AuroraOS revision this app is known to build
against. Override the variables if you are deliberately testing another
one:

```text
make AURORA_COMMIT=<sha>
make AURORA_DIR=/path/to/AuroraOS
```

Useful defines compiled in (the same flags for the host tests):

| Define | Why |
|--------|-----|
| `NOZIP_SUPPORT` | SMS Plus does not open zip files |
| `MAXIM_PSG` | SMS Plus PSG core |
| `USE_Z80` | SMS Plus Z80 |

The app icon is `src/RetroArch.icon` (a 32×32 picture). Version
`0.1.4` is `RA_VERSION` in `src/retroarch.h`.

## Host tests

```text
make test
```

This compiles `build/test_cores` with the host compiler and runs it. The
cores do not call the Aurora SDK, so the tests run on the PC. They use
synthetic Game Boy and NES ROMs, not files from a cartridge. A failure
here is a core problem, not an SD-card problem.

## Clean

```text
make clean       # deletes build/, RetroArch.bin, and the SDK's build output
make distclean   # clean, and also deletes the cloned third_party/AuroraOS tree
```

`make clean` removes the `RetroArch.bin` in the repository root. It does
not remove `dist/RetroArch.bin`.

## Source map

```text
src/main.c              entry, run loop, content loading, OSD
src/menu.c              menu, file browser, settings, information
src/video.c             blit to Aurora's 24-bit top screen, scale, frameskip
src/input.c             3DS buttons to RetroPad port 0
src/environ.c           libretro environment
src/audio.c             stereo PCM played on CSND voices
src/saves.c             paths, retroarch.cfg, SRAM, save states
src/retroarch.h         RA_VERSION, config struct, shared declarations
src/cores/core.h        static-core descriptor
src/cores/cores.c       core list and extension match
src/cores/core_gb.c     Peanut-GB
src/cores/core_nes.c    agnes
src/cores/core_sms.c    SMS Plus GX, including BIOS.col
src/cores/core_chip8.c  CHIP-8 interpreter
compat/dirent.h         dirent for a plain newlib toolchain
tests/host/             host core tests
third_party/libretro/   libretro.h (MIT)
third_party/peanut_gb/  Peanut-GB (MIT)
third_party/agnes/      agnes (MIT)
third_party/smsplus/    SMS Plus GX (GPL-2.0 or later)
```

AuroraOS itself is cloned into `third_party/AuroraOS` and is not part of
this repository's license header. It stays under its own license.
