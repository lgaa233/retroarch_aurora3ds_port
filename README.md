# RetroArch for AuroraOS (Nintendo 3DS)

A working RetroArch-style frontend for
[AuroraOS](https://github.com/TeamAuroraOS/AuroraOS), the bare-metal
custom OS for the Nintendo 3DS. Picture, menu, saves, save states, and
PCM sound work. Written in C against the
[Aurora C SDK](https://github.com/TeamAuroraOS/AuroraOS/tree/main/sdk).

It is a libretro frontend in the shape of RetroArch: the same
frontend-and-cores architecture, the libretro API (`libretro.h`, unmodified),
an RGUI-style menu, save states, SRAM saves, a `retroarch.cfg`, and statically
linked cores. Mainline RetroArch itself cannot run inside AuroraOS app
constraints (one thread, 16 MB heap, apps on the ARM9 @ 134 MHz with no FPU,
no dynamic loader), so the frontend is a compact reimplementation on the same
API — cores written for libretro port over with their entry points intact.

Works on **every 3DS model** (Old/New 3DS, 2DS, XL): Aurora apps run on the
ARM9, which is identical across the family.

## Wiki

How to install AuroraOS and RetroArch, load ROMs, use the menu, and read
the limits of this version is in the [wiki](wiki/Home.md).

## Cores

| Core | System | Extensions | Save states | SRAM |
|------|--------|-----------|-------------|------|
| [Peanut-GB](https://github.com/deltabeard/Peanut-GB) | Game Boy (DMG) | `.gb` `.dmg` | yes | yes (battery saves) |
| [SMS Plus GX](https://github.com/libretro/smsplus-gx) | Sega Master System | `.sms` | yes | yes (battery saves) |
| | Sega Game Gear | `.gg` | yes | yes |
| | Sega SG-1000 | `.sg` | yes | — |
| | ColecoVision | `.col` | yes | — |
| chip8 (this repo) | CHIP-8 | `.ch8` | yes | — |

All are integer-only emulators chosen for the FPU-less 134 MHz ARM9. There is
no dynamic loader on AuroraOS, so cores are linked in behind a descriptor
struct, the way RetroArch's own static (griffin) builds link cores.

ColecoVision games need an 8192-byte BIOS dump named `BIOS.col` in
`SD:/Aurora/ROMs`. This port does not load a Master System `bios.sms`.

## Install

1. Install [AuroraOS](https://github.com/TeamAuroraOS/AuroraOS) (Beta 6
   v0.1.5 or later — the app is an `AURC` container).
2. Copy `RetroArch.bin` to `SD:/Aurora/Apps/C/`.
3. Put ROMs in `SD:/Aurora/ROMs/` (made on first start; subfolders work).
4. Start **RetroArch** from the Home Menu.

Saves land in `SD:/Aurora/Apps/C/RetroArch/saves/`, save states in
`.../states/`, settings in `.../retroarch.cfg`.

## Controls

| Input | Action |
|-------|--------|
| D-pad / Circle Pad | RetroPad d-pad; menu navigation |
| A / B / X / Y / L / R / START / SELECT | the matching RetroPad buttons |
| Touch the bottom screen | open the RetroArch menu |
| A / B in the menu | choose / back |
| L / R in the file browser | page up / down |
| Hold R in game | fast-forward (enable it in Settings first) |
| HOME | back to the Aurora Home Menu (SRAM is saved on the way out) |

Per-system notes: on the Game Gear START is the Start button and on the
Master System it is Pause. ColecoVision keypad keys ride on X=1, Y=2, R=3,
L=4, START=# and SELECT=\*. The CHIP-8 4x4 keypad maps D-pad to 2/4/6/8,
A=5, B=6, X=1, Y=3, L=A, R=B, START=C, SELECT=D.

Menu: Resume, Restart, Save/Load State (10 slots), Load Content, Settings
(video scale 1x / fit / stretch, frameskip 0–4, FPS counter, fast-forward,
volume), Information.

## Building

Needs `arm-none-eabi-gcc` on `PATH` (devkitARM, or Ubuntu/Debian
`gcc-arm-none-eabi` + `libnewlib-arm-none-eabi` — `compat/dirent.h` makes the
SDK build with the latter), `python3` and `git`:

```
make          # clones AuroraOS (pinned commit) and builds RetroArch.bin
make test     # host-side core, scaler and audio tests (plain gcc)
```

`make` also builds the ARM11 scaler job (`arm11/`) with `arm-none-eabi-gcc`
and embeds it in the app.

A prebuilt `dist/RetroArch.bin` is checked in.

## Limitations

* **Sound is mono.** Aurora's CSND voices play the same volume on both
  speakers, so the cores' stereo is mixed down, resampled to the voice's
  32776 Hz with dynamic rate control (±0.5%), and played on one voice. A
  Volume setting (0–125%) sits in Settings.
* The cores run on the 134 MHz ARM9. The scaler (all three scale modes) runs
  as an ARM11 job when the AuroraOS core is version 116 or later (`AUDIO_CMD_RUN11`);
  older cores fall back to the ARM9 scaler, which is slower. Show FPS prints
  which one is in use and its time per frame.
* Save states are tied to the same ROM and the same RetroArch build.
* The New 3DS extra buttons (ZL/ZR, C-stick) are not readable from the SDK.
* One content at a time, RetroPad port 0 only, no cheats/shaders/netplay.

## Source layout

```
src/main.c           entry point, the run loop, content loading, the OSD
src/menu.c           RGUI-style menu: browser, settings, information
src/video.c          frame to Aurora's rotated 24-bit framebuffer (ARM11 or ARM9)
arm11/               the integer scaler, shared by the ARM11 job and the ARM9
src/input.c          3DS buttons -> RetroPad
src/environ.c        RETRO_ENVIRONMENT answers and the video callback
src/audio.c          mono PCM ring, resampler and rate control on CSND voice 6
src/saves.c          paths, retroarch.cfg, SRAM, save states
src/cores/core.h     the static-core descriptor (libretro signatures)
src/cores/core_gb.c  Peanut-GB wrapper (plus its own serialize format)
src/cores/core_sms.c SMS Plus GX wrapper (SMS, GG, SG-1000, ColecoVision)
src/cores/core_chip8.c  a CHIP-8 interpreter written for this frontend
compat/dirent.h      devkitARM-style <dirent.h> for plain newlib toolchains
tests/host/          core, scaler and audio tests (synthesised ROMs)
third_party/         vendored libretro.h and Peanut-GB (both MIT)
```

## License

GPL-3.0 (like RetroArch and AuroraOS; an app built with the Aurora SDK links
GPL code from the OS). Vendored components keep their own licenses:
`libretro.h` (MIT), Peanut-GB (MIT), SMS Plus GX (GPL-2.0 or
later; Charles MacDonald, Eke-Eke, gameblabla — see the headers in
`third_party/smsplus/`).
