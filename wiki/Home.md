# RetroArch for AuroraOS

RetroArch for AuroraOS is an unofficial libretro frontend for
[AuroraOS](https://github.com/TeamAuroraOS/AuroraOS) on the Nintendo 3DS.
Version **0.1.6** runs on every 3DS model (original 3DS, 3DS XL, 2DS, New
3DS, New 3DS XL, and New 2DS XL). Aurora apps run on the ARM9, and that CPU
is the same chip across the family: ARM9 at 134 MHz, with no floating-point
unit.

This is a RetroArch-style frontend, not a build of mainline RetroArch.
Mainline RetroArch does not fit AuroraOS app limits (one thread, a 16 MB
heap, no dynamic loader, no FPU). The program in this repository speaks the
libretro API (`libretro.h`, unmodified), ships an RGUI-style menu, SRAM
saves, save states, and a `retroarch.cfg`, and links its cores into the
binary.

The picture is on the top screen. The bottom screen is status and help.
Picture, menu, SRAM saves, save states, and stereo PCM all work on the
console. Game Boy, Master System, Game Gear, SG-1000, ColecoVision, and
CHIP-8, and NES have PCM sound (NES uses agnes for gameplay plus an
integer 2A03 APU).

Source code and the prebuilt `dist/RetroArch.bin` live at
<https://github.com/lgaa233/retroarch_aurora3ds_port>. These pages are the
project wiki. They are stored in the repository under `wiki/` so they can
be read on GitHub without the Wiki tab. The filenames match GitHub Wiki
page names (`Using-the-App.md` is the page "Using the App") if the same
text is later copied into that tab.

## What it plays

| Core | System | Extensions | Battery save | Save states |
|------|--------|------------|--------------|-------------|
| Peanut-GB | Game Boy (DMG) | `.gb` `.dmg` | yes | yes |
| agnes 0.2.0 | NES / Famicom | `.nes` | no | yes |
| SMS Plus GX | Master System, Game Gear, SG-1000, ColecoVision | `.sms` `.gg` `.sg` `.col` | 32 KB `.srm` for every game | yes |
| chip8 1.0 | CHIP-8 | `.ch8` | no | yes |

ColecoVision will not boot without your own 8192-byte `BIOS.col` in the
ROM folder. Details are on [Supported Systems](Supported-Systems.md).

## Where files go

On a card reader the SD card root is `SD:`. Inside Aurora that same root
is `/`.

| What | On the SD card | Inside Aurora |
|------|----------------|----------------|
| The app | `SD:/Aurora/Apps/C/RetroArch.bin` | `/Aurora/Apps/C/RetroArch.bin` |
| Data directory | `SD:/Aurora/Apps/C/RetroArch/` | the directory `app_dir()` returns for that binary |
| Settings | `SD:/Aurora/Apps/C/RetroArch/retroarch.cfg` | `<app dir>/retroarch.cfg` |
| SRAM saves | `SD:/Aurora/Apps/C/RetroArch/saves/` | `<app dir>/saves/` |
| Save states | `SD:/Aurora/Apps/C/RetroArch/states/` | `<app dir>/states/` |
| ROMs and `BIOS.col` | `SD:/Aurora/ROMs/` | `/Aurora/ROMs` |

The data directory is created on first launch, next to the `.bin`, and
named after the file. `RetroArch.bin` uses `RetroArch/`. The Information
screen prints the save and state paths the running app is actually using.

## Pages

* [Installation](Installation.md) — AuroraOS, copying the `.bin`, first launch
* [Using the App](Using-the-App.md) — menu, screens, quitting
* [Loading ROMs](Loading-ROMs.md) — folders, the browser, `BIOS.col`
* [Controls](Controls.md) — buttons, menu, per-system keypad maps
* [Supported Systems](Supported-Systems.md) — what each core actually runs
* [Saves and Save States](Saves-and-Save-States.md) — `.srm` files and slots 0–9
* [Settings](Settings.md) — video, frameskip, FPS, fast-forward, `retroarch.cfg`
* [Building from Source](Building-from-Source.md) — toolchain, `make`, `make test`
* [Troubleshooting](Troubleshooting.md) — install, BIOS, slow games, silence
* [Limitations](Limitations.md) — what this version does and does not do
* [License](License.md) — GPL-3.0 and the vendored cores
