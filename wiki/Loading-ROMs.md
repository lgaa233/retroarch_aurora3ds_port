# Loading ROMs

Put your own game dumps in `SD:/Aurora/ROMs`. Inside Aurora that folder is
`/Aurora/ROMs`. The app creates it on first launch. Subfolders work, and
the browser can walk above this folder with `..` if a file lives somewhere
else on the card.

This project does not provide ROMs or BIOS images, and this wiki does not
link to any. Use dumps from cartridges and consoles you own. CHIP-8
programs are a special case: classic and modern `.ch8` games are typically
public domain, and you still copy those files onto the card yourself.

## Extensions the browser shows

The browser lists **directories** and files whose extension matches a
core. Matching ignores case, so `.GB` and `.gb` are both Game Boy.

| Extension | System |
|-----------|--------|
| `.gb` `.dmg` | Game Boy (DMG) |
| `.sms` | Sega Master System |
| `.gg` | Sega Game Gear |
| `.sg` | Sega SG-1000 |
| `.col` | ColecoVision |

Anything else is hidden. That includes `.gbc`, `.gba`, `.zip`, `.7z`,
`.srm`, and save-state files. There is no zip support: the SMS Plus core
is built with zip disabled, and the browser would hide an archive even if
a core could open one. Extract the ROM first.

The core is chosen from the extension only. A Master System game renamed
to `.gg` is handed to the Game Gear path.

## ColecoVision BIOS

Every `.col` game needs the ColecoVision BIOS in the **system directory**.
This frontend reports that directory as `/Aurora/ROMs`, and the core opens
exactly:

```text
/Aurora/ROMs/BIOS.col
```

On the card, that is `SD:/Aurora/ROMs/BIOS.col`.

* The file must be **8192 bytes** (8 KB). Any other length is rejected.
* It must sit in `/Aurora/ROMs` itself, not in a subfolder. The browser's
  last directory does not change where the BIOS is loaded from.
* The name the core opens is `BIOS.col`.
* This port does not load a Master System BIOS. A file named `bios.sms`
  is ignored, even if it is sitting next to the ROMs.

If `BIOS.col` is missing or the wrong size, loading the game fails with
`Could not load <name>`. The BIOS has to be a dump from a console you own.

## What the browser does

* Folders are drawn in green as `[name]` and are sorted first. Files
  follow, A to Z, ignoring case.
* At most **256** folders and recognized ROMs are kept. If reading stops
  because of that cap, the top screen says `(first 256 entries shown)`.
  Split a huge folder into subfolders.
* Hidden files (wrong extension) do not take one of the 256 slots, but
  once 256 good entries are stored the rest of the directory is not read.
* An empty result says `(no folders or known ROMs here)`.
* `..` is shown whenever you are not at `/`. A on `..`, or B, goes to the
  parent. B at `/` closes the browser.
* **L** and **R** move the cursor by one page (16 rows). Up and Down
  repeat if you hold them.
* A on a ROM loads it. On success the current folder is stored as
  `last_dir` in `retroarch.cfg`, and the next **Load Content** opens that
  folder when it still exists.

Only one ROM is loaded at a time. Starting another one saves the previous
game's SRAM and then loads the new file.

## Names that share a save

SRAM files and save states are named from the file name with the directory
and the **last** extension removed.

| ROM path | Save name |
|----------|-----------|
| `/Aurora/ROMs/Tetris.gb` | `Tetris` |
| `/Aurora/ROMs/gb/Tetris.gb` | `Tetris` |
| `/Aurora/ROMs/sms/My.Game.sms` | `My.Game` |

`Tetris.gb` and `Tetris.sms` in different folders both use `Tetris.srm` and
`Tetris.state0`. Give dumps distinct base names when they are different
games. The base name is cut at 63 characters.

## Sizes the cores refuse

A file with the right extension can still fail `load`:

| Core | Rejected when |
|------|----------------|
| Peanut-GB | smaller than 336 bytes (`0x150`), or Peanut-GB rejects the header |
| SMS Plus GX | smaller than 1024 bytes (`0x400`); ColecoVision also fails without a valid `BIOS.col` |
| chip8 | empty, or larger than 3584 bytes (the machine has 4096 bytes of RAM and programs start at `0x200`) |

More on each system is in [Supported Systems](Supported-Systems.md).
