# Installation

RetroArch for AuroraOS 0.1.3 is an `AURC` container. It needs **AuroraOS
Beta 6 v0.1.5 or later**. Older AuroraOS builds cannot launch this file.

You also need a Nintendo 3DS that already runs Luma3DS, and an SD card you
can write from a PC. ROMs and the ColecoVision BIOS must be dumps you made
from hardware you own. This wiki does not link to ROM or BIOS downloads.
CHIP-8 programs (`.ch8`) are typically public domain; they are still files
you supply yourself.

The prebuilt app in the repository is [`dist/RetroArch.bin`](../dist/RetroArch.bin).
Building your own binary is covered in
[Building from Source](Building-from-Source.md).

## Install AuroraOS

The current AuroraOS files and any newer install notes are in the
[AuroraOS repository](https://github.com/TeamAuroraOS/AuroraOS). The boot
path this app expects is:

1. Copy `Aurora.firm` to `SD:/luma/payloads/`.
2. Copy `AURORAOS.BIN` to the root of the SD card.
3. Optionally copy `assets.pak` to `SD:/Aurora/assets.pak`.
4. In Luma3DS, enable loading external FIRMs.
5. Power the console off, hold **START** while powering on, select
   **Aurora**, and choose **Boot Aurora**.

After that, the Aurora Home Menu is where RetroArch appears.

## Install RetroArch

1. On the SD card, create `SD:/Aurora/Apps/C/` if that folder is not
   already there. Aurora lists C apps from this directory.
2. Copy `RetroArch.bin` to `SD:/Aurora/Apps/C/RetroArch.bin`.
3. Boot Aurora and start **RetroArch** from the Home Menu.

Leave the file name as `RetroArch.bin` unless you want a different data
folder. The app stores its saves beside the binary, in a directory named
after the file with `.bin` removed.

## What the first launch creates

On startup the app creates:

* `SD:/Aurora/ROMs/` — put game files here. Subfolders are fine.
* `SD:/Aurora/Apps/C/RetroArch/saves/` — cartridge RAM (`.srm`)
* `SD:/Aurora/Apps/C/RetroArch/states/` — save states (`.state0` … `.state9`)

`retroarch.cfg` is written in `SD:/Aurora/Apps/C/RetroArch/` when you leave
Settings, when a ROM loads (the last folder is remembered), and when you
exit, including with **HOME**.

You can copy ROMs into `SD:/Aurora/ROMs/` before the first launch. The
folder is created either way. See [Loading ROMs](Loading-ROMs.md) for
extensions, the 256-entry browser, and ColecoVision's `BIOS.col`.

## Updating

Replace `SD:/Aurora/Apps/C/RetroArch.bin` with the new file. Leave
`SD:/Aurora/Apps/C/RetroArch/` in place if you want to keep settings,
saves, and states.

Save states are tied to the same ROM **and** the same RetroArch build. A
state written by one build can fail to load in another (`Bad state file`).
SRAM `.srm` files are raw cartridge RAM and are the files to keep across
updates. See [Saves and Save States](Saves-and-Save-States.md).

## Removing

Delete `SD:/Aurora/Apps/C/RetroArch.bin` and, if you also want the saves
gone, `SD:/Aurora/Apps/C/RetroArch/`. ROMs in `SD:/Aurora/ROMs/` are not
removed with the app. `BIOS.col` lives in that ROM folder too.
