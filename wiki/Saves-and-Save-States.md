# Saves and Save States

Two different files remember progress. They are stored under the app data
directory, which for the usual install is
`SD:/Aurora/Apps/C/RetroArch/`.

| Kind | Path | When it is written |
|------|------|--------------------|
| Cartridge RAM | `saves/<romname>.srm` | unload of that game, **Quit RetroArch**, and **HOME** (the exit handler) |
| Save state | `states/<romname>.state0` … `<romname>.state9` | **Save State** in the menu |

`<romname>` is the file name without its folder and without the last
extension. `SD:/Aurora/ROMs/rpg/Quest.gb` and
`SD:/Aurora/ROMs/Quest.nes` both use the name `Quest`. The name is limited
to 63 characters. See [Loading ROMs](Loading-ROMs.md).

The Information screen prints the real `saves` and `states` directories
for the copy that is running. Renaming `RetroArch.bin` changes `app_dir()`,
and the folders follow the new name.

## SRAM (`.srm`)

On a successful load the frontend copies `saves/<romname>.srm` into the
core's save RAM, if the core exposes any and the file exists. A missing
file is not an error: the core keeps its own initial RAM (Game Boy cart
RAM starts zeroed).

The file is written from whatever is in that RAM when:

* you load a different ROM (the previous game is unloaded first)
* you choose **Quit RetroArch**
* you press **HOME**, or the process otherwise exits (`atexit` saves SRAM
  and `retroarch.cfg`)

**Restart** resets the machine and does not delete the file on the card.

Which cores actually have save RAM:

| Core | `.srm` |
|------|--------|
| Peanut-GB | raw cart RAM. The size is whatever the Game Boy header reports. A cart with no RAM does not get a file |
| agnes | never. The core returns no save RAM. NES battery progress is only in save states |
| SMS Plus GX | always 32768 bytes, for `.sms`, `.gg`, `.sg`, and `.col`. The file is the core's whole cart-RAM buffer, including games that had no battery |
| chip8 | never |

These are this frontend's dumps. They are not save-state files, and they
are not guaranteed to match another emulator's `.srm` layout. The SMS Plus
file is always 32 KB even when the useful battery data is a smaller region
at the start of that buffer.

Loading a save state replaces the RAM inside the running machine,
including cart RAM that the state captured. The `.srm` on the card is
updated later, from that memory, on the next unload or exit.

## Save states

The menu row **State Slot** is a number from 0 to 9. Left and Right change
it. A on that row advances it. The slot resets to **0** every time a ROM
loads. The slot number is not stored in `retroarch.cfg`.

**Save State** stays in the menu and writes:

```text
states/<romname>.state<N>
```

The status line reports one of:

* `Saved state to slot N`
* `SD write failed`
* `Out of memory (… KB)`
* `Core failed to serialize`
* `No content running`
* `Core cannot save states`

**Load State** reads that file.

* Success closes the menu and shows `Loaded state from slot N` for about
  two seconds.
* `No state in slot N` means the file is not there.
* `Bad state file` means the header did not match this ROM on this build.

### Why a state is refused

Each core puts a magic and a version (`1` in this release) at the start
of the file, then checks that the state still belongs to the ROM that is
loaded:

| Core | Magic | Also checked |
|------|-------|----------------|
| Peanut-GB | `PGBS` | version, size of `struct gb_s`, ROM size, cart-RAM size |
| agnes | `AGNS` | version, agnes state size, ROM size |
| SMS Plus GX | `SMSS` | version, state size, CRC of the loaded ROM |
| chip8 | `CHP8` | version, ROM size |

A state from a different game, a truncated file, or a RetroArch build
whose core structs changed will not load. Keep `.srm` files if you need
progress to survive an update; treat `.stateN` files as valid for the
build that wrote them.

States are not compressed. They are not mainline RetroArch state files
and will not load in the desktop RetroArch application.
