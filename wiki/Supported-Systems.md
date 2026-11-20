# Supported Systems

Version 0.1.0 links four cores into `RetroArch.bin`. There is no core
downloader and no dynamic loader. A system that is not in the table is
not playable by renaming the file.

All four cores are integer-only, which is why they were chosen for an
ARM9 with no FPU. The frontend throws audio samples away. See
[Limitations](Limitations.md).

| Core (as shown in Information) | System | Extensions | SRAM `.srm` | Save states |
|--------------------------------|--------|------------|-------------|-------------|
| Peanut-GB `head` | Game Boy, original DMG | `.gb` `.dmg` | yes, when the cart header has RAM | yes |
| agnes `0.2.0` | NES / Famicom | `.nes` | no | yes |
| SMS Plus GX `head` | Master System, Game Gear, SG-1000, ColecoVision | `.sms` `.gg` `.sg` `.col` | always 32 KB | yes |
| chip8 `1.0` | CHIP-8 | `.ch8` | no | yes |

Button maps are on [Controls](Controls.md). File names and the BIOS are
on [Loading ROMs](Loading-ROMs.md).

## Game Boy (Peanut-GB)

[Peanut-GB](https://github.com/deltabeard/Peanut-GB) emulates the original
Game Boy (DMG). It is not a Game Boy Color or Game Boy Advance emulator.
`.gbc` and `.gba` files do not appear in the browser.

The picture is the DMG green palette at 160×144. Sound is compiled out of
this core (`ENABLE_SOUND` is 0).

Cartridge RAM size comes from the cart header. When that size is non-zero
the frontend loads and writes `<romname>.srm`. Carts with no RAM do not
get an `.srm`. Save states include the CPU state and that cart RAM, and
they only load back into the same ROM on a build whose `struct gb_s` is
the same size.

## NES / Famicom (agnes)

[agnes](https://github.com/kgabis/agnes) 0.2.0 loads iNES (`.nes`) files.
The mappers it implements are:

| iNES mapper | Common name |
|-------------|-------------|
| 0 | NROM |
| 1 | MMC1 |
| 2 | UxROM |
| 4 | MMC3 |

Any other mapper fails to load (`Could not load …`). The picture is
256×240. One standard controller is read.

agnes does not expose cartridge RAM through the libretro save-RAM API, so
this frontend never writes an `.srm` for NES games. Battery-backed
progress exists only inside a save state. The state header stores the ROM
size and the agnes state size; a mismatch is `Bad state file`.

## Master System, Game Gear, SG-1000 (SMS Plus GX)

[SMS Plus GX](https://github.com/libretro/smsplus-gx) (Charles MacDonald,
Eke-Eke, gameblabla) emulates all three machines with one Z80 core. The
extension selects SG-1000 (`.sg`) and forces a headerless Game Gear dump
(`.gg`) onto the Game Gear when the header check did not. Other files are
detected from the ROM header and the core's built-in game database
(region, display, mapper).

There is no menu to pick Japan, export, NTSC, or PAL. Whatever the header
and the database decide is what runs.

The wrapper asks the core not to enable FM up front, because nothing can
play it. The game database can still mark a known title as FM-capable,
and that work still runs. The samples are discarded either way. The
Master System per-line sprite limit stays on.

A Master System BIOS is **not** loaded. Do not look for `bios.sms`
support in this build; the frontend never opens that file. Upstream SMS
Plus knows how to use a BIOS, and this wrapper does not call that path.

### The 32 KB `.srm`

For every game this core loads — Master System, Game Gear, SG-1000, and
ColecoVision — the frontend is shown the whole `cart.sram` buffer, 32768
bytes (`0x8000`), as `RETRO_MEMORY_SAVE_RAM`. Leaving the game therefore
writes a 32 KB `.srm` even when the original cart had no battery (SG-1000
and ColecoVision included). The file is that buffer, not a trimmed
battery image.

## ColecoVision (SMS Plus GX)

ColecoVision is the same core, selected by `.col`. It will not start
unless this exact file loads:

```text
/Aurora/ROMs/BIOS.col
```

8192 bytes, in the ROM folder, not in a subfolder. The system directory
the frontend reports to cores is `/Aurora/ROMs`. A missing or wrong-sized
BIOS fails the load before the game runs.

The keypad mapping covers 1, 2, 3, 4, `*`, and `#` only. See
[Controls](Controls.md). A 32 KB `.srm` is still written, for the same
reason as the other SMS Plus systems.

## CHIP-8

The CHIP-8 core is written for this frontend. It is the 1977 COSMAC VIP
virtual machine: 4096 bytes of RAM, a 64×32 monochrome screen, and 16
keys. Programs are loaded at address `0x200`, so a `.ch8` file must be
from 1 to 3584 bytes.

Timing is 14 instructions per frame, about 840 instructions per second,
which is a comfortable VIP pace. Quirks, fixed in this interpreter:

* Shift instructions write the shifted value of VY into VX, as on the VIP.
* Load and store (`Fx55` / `Fx65`) leave I where it is, which is what most
  modern CHIP-8 programs expect.

Keys 0, 7, 9, E, and F are not on the 3DS pad. The sound timer decrements
and no beep is played. Save states restore the whole machine, including
the 64×32 screen. There is no `.srm`.

At **1x** a CHIP-8 picture is a 64×32 block in the center of the top
screen. **Fit** enlarges it to 400×200. See [Settings](Settings.md).
