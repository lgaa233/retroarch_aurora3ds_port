# Troubleshooting

Messages below are the strings the app actually prints. Paths assume the
usual install: `RetroArch.bin` in `SD:/Aurora/Apps/C/`. The Information
screen is the place to confirm the save and state directories of the
binary you launched.

## RetroArch does not appear in the Aurora Home Menu

* The file must be `SD:/Aurora/Apps/C/RetroArch.bin`. A copy in
  `SD:/Aurora/Apps/` or on the card root is not listed as a C app.
* The file has to be the `AURC` container (`dist/RetroArch.bin`, or the
  `RetroArch.bin` that `make` writes). A host `build/test_cores` binary
  will not run on the 3DS.
* AuroraOS must be **Beta 6 v0.1.5 or later**. This app is an `AURC`
  container, and older AuroraOS builds do not launch that format.
* Confirm Aurora itself boots: `Aurora.firm` in `SD:/luma/payloads/`,
  `AURORAOS.BIN` on the SD root, external FIRMs enabled in Luma3DS, then
  power on while holding START. See [Installation](Installation.md) and
  the [AuroraOS repository](https://github.com/TeamAuroraOS/AuroraOS).

## The ROM browser is empty

The status line `(no folders or known ROMs here)` means this folder has
no subdirectories and no file with a known extension.

* ROMs belong in `SD:/Aurora/ROMs`, which the app creates on first launch.
  On a PC you create that folder yourself and copy dumps in.
* Extensions are `.gb`, `.dmg`, `.nes`, `.sms`, `.gg`, `.sg`, `.col`, and
  `.ch8` only. `.gbc`, `.gba`, `.zip`, and `.7z` are hidden.
* Zip files are not opened. Extract them and keep the raw ROM.
* If the line says `(first 256 entries shown)`, the folder has more than
  the browser will list. Move games into subfolders of at most 256
  recognized files and folders.

**Load Content** reopens `last_dir` from `retroarch.cfg`. If that folder
was deleted, the browser uses `/Aurora/ROMs` instead. B moves up toward
`/`.

## `Could not load <name>`

The file was visible, and the core rejected it.

* ColecoVision: `/Aurora/ROMs/BIOS.col` must exist and be **exactly 8192
  bytes**. A BIOS in a subfolder does not count. `bios.sms` is not read
  by this port and will not satisfy ColecoVision.
* NES: agnes only loads iNES mappers 0, 1, 2, and 4. Other mappers fail
  here.
* Game Boy: the file must be a DMG ROM Peanut-GB accepts, at least 336
  bytes. Game Boy Color-only dumps are outside this core.
* Master System / Game Gear / SG-1000: the file must be at least 1024
  bytes.
* CHIP-8: the file must be 1 to 3584 bytes.
* A corrupt or truncated dump fails the same way.

The size and mapper rules are collected on
[Loading ROMs](Loading-ROMs.md) and
[Supported Systems](Supported-Systems.md).

## A game is silent

Sound works on all built-in cores, including NES. If a game is silent,
confirm you launched RetroArch from the Aurora Home Menu (not a direct
`*.3dsx` shortcut) and that you resumed after opening the in-game menu.

Open RetroArch from the Aurora Home Menu. The ARM11 core, which owns
the speakers, is only there for an app started that way. Opening the
RetroArch menu stops the voices until you resume. Details are on
[Limitations](Limitations.md).

## The game is slow or stuttering

The cores run on the ARM9 at 134 MHz, with no FPU. The scaling runs on the
ARM11 only when the AuroraOS core is version 116 or later.

* Turn on **Show FPS**. If it says `ARM9` instead of `ARM11`, update
  AuroraOS: the ARM9 fallback scaler costs much more of each frame.
* Set **Frameskip** to 1–4. The game logic still advances; fewer frames
  are drawn. Frameskip 1 draws every other frame.
* With the ARM9 scaler, prefer **1x** or **Fit** over **Stretch**.
  Stretch always paints 400×240.
* **Show FPS** counts main-loop frames over the last second. It does not
  count the extra fast-forward runs.
* Some Master System titles are marked FM-capable. That synthesis runs
  and is heard, and it costs extra ARM9 time.

**Fast Fwd (hold R)** makes the game run faster. It does not make a slow
game smoother. Enable it in Settings, then hold R during play. On
ColecoVision that hold is also keypad 3. On CHIP-8 it is also key B.

## Saves

| What you see | What it means |
|--------------|----------------|
| No `.srm` for a Game Boy | the header reported no cart RAM, or you have not left the game yet |
| No `.srm` for NES or CHIP-8 | those cores do not expose save RAM. Use a save state |
| A 32 KB `.srm` for SG-1000 or ColecoVision | expected. SMS Plus exposes a 32 KB buffer for every one of its games |
| Progress vanished but the `.srm` exists | another ROM with the same base name shares the file (`Quest.gb` and `Quest.nes` both use `Quest.srm`) |
| `No state in slot N` | that `.stateN` has not been written |
| `Bad state file` | different ROM, truncated file, or a state from another RetroArch build |
| `SD write failed` | the card did not accept the write |
| `Out of memory` | the state buffer could not be allocated |

SRAM is written when you load a different game, quit from the menu, or
press HOME. Pulling the card while the game is still running skips that
write. Save states are written only when you choose **Save State**.

State slot goes back to 0 whenever a ROM loads. The slot is not kept in
`retroarch.cfg`.

## Controls that seem dead

* Fast-forward requires the Settings toggle **and** holding R in the game,
  not in the menu.
* Game Gear START is Start. Master System and SG-1000 START is Pause.
* ColecoVision keypad keys on the 3DS are 1 (X), 2 (Y), 3 (R), 4 (L),
  `#` (START), and `*` (SELECT). Keys 0 and 5–9 are not mapped.
* CHIP-8 keys 0, 7, 9, E, and F are not mapped.
* ZL, ZR, and the C-stick are not readable. Only port 0 is read.
* L and R page the file browser. They do not page the main menu.

## Settings did not stick

`retroarch.cfg` is rewritten when you leave Settings, when a ROM loads
(for `last_dir`), and on exit. A value of `scale` outside 0–2 becomes
Fit the next time the file is loaded. `frameskip` outside 0–4 becomes 0.

Edit the file while RetroArch is not running, or the exit rewrite will
replace it with the values from the session.

## What this version does

Version 0.1.7 runs on the console. Picture, the menu, saves, save
states, and PCM sound work on all built-in cores, including NES.
`make test` also runs the cores on a PC with synthetic ROMs.
[Limitations](Limitations.md) lists the boundaries that are still real,
such as mono sound, NES APU IRQs that do not reach the CPU, and no New 3DS
extra buttons.
