# RetroArch for AuroraOS (Nintendo 3DS)

A port of RetroArch to [AuroraOS](https://github.com/TeamAuroraOS/AuroraOS),
the bare-metal custom OS for the Nintendo 3DS — written in C against the
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

## Cores

| Core | System | Extensions | Save states | SRAM |
|------|--------|-----------|-------------|------|
| [Peanut-GB](https://github.com/deltabeard/Peanut-GB) | Game Boy (DMG) | `.gb` `.dmg` | yes | yes (battery saves) |
| [agnes](https://github.com/kgabis/agnes) | NES / Famicom | `.nes` | yes | — |

Both are integer-only emulators chosen for the FPU-less 134 MHz ARM9. There is
no dynamic loader on AuroraOS, so cores are linked in behind a descriptor
struct, the way RetroArch's own static (griffin) builds link cores.

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

Menu: Resume, Restart, Save/Load State (10 slots), Load Content, Settings
(video scale 1x / fit / stretch, frameskip 0–4, FPS counter, fast-forward),
Information.

## Building

Needs `arm-none-eabi-gcc` on `PATH` (devkitARM, or Ubuntu/Debian
`gcc-arm-none-eabi` + `libnewlib-arm-none-eabi` — `compat/dirent.h` makes the
SDK build with the latter), `python3` and `git`:

```
make          # clones AuroraOS (pinned commit) and builds RetroArch.bin
make test     # host-side core tests with synthetic ROMs (plain gcc)
```

A prebuilt `dist/RetroArch.bin` is checked in.

## Limitations

* **No audio.** The Aurora SDK plays preloaded WAV voices only; there is no
  PCM streaming an emulator could feed. When the SDK grows one, the audio
  callbacks in `src/environ.c` are the single place to wire it up.
* Speed depends on the game: the whole emulator runs on the 134 MHz ARM9.
  Frameskip and the stretch-free scale modes help.
* Save states are tied to the same ROM and the same RetroArch build.
* The New 3DS extra buttons (ZL/ZR, C-stick) are not readable from the SDK.
* One content at a time, RetroPad port 0 only, no cheats/shaders/netplay.

## Source layout

```
src/main.c           entry point, the run loop, content loading, the OSD
src/menu.c           RGUI-style menu: browser, settings, information
src/video.c          blitter to Aurora's rotated 24-bit framebuffer + scaling
src/input.c          3DS buttons -> RetroPad
src/environ.c        RETRO_ENVIRONMENT answers, video/audio/input callbacks
src/saves.c          paths, retroarch.cfg, SRAM, save states
src/cores/core.h     the static-core descriptor (libretro signatures)
src/cores/core_gb.c  Peanut-GB wrapper (plus its own serialize format)
src/cores/core_nes.c agnes wrapper (direct PPU buffer -> RGB565 LUT)
compat/dirent.h      devkitARM-style <dirent.h> for plain newlib toolchains
tests/host/          core smoke tests with synthesised GB/NES ROMs
third_party/         vendored libretro.h, Peanut-GB, agnes (all MIT)
```

## License

GPL-3.0 (like RetroArch and AuroraOS; an app built with the Aurora SDK links
GPL code from the OS). Vendored components keep their own licenses:
`libretro.h` (MIT), Peanut-GB (MIT), agnes (MIT).
