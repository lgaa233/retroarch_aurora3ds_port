# Limitations

This is RetroArch **0.1.6** for AuroraOS: a working libretro frontend with
four statically linked cores. Picture, controls, the menu, SRAM saves,
save states, and PCM sound work on the console. The list below is what
this version does, not a list of broken features.

## Hardware it runs on

Aurora apps execute on the ARM9 at **134 MHz with no FPU**, on every
model. The New 3DS does not give this app a faster CPU, a second core,
or the extra controls. ZL, ZR, and the C-stick are not readable through
the Aurora SDK this build uses.

The top screen is 400×240 and is where the game and the menu are drawn.
The bottom screen is status and help. There is no stereoscopic 3D.

The heap and the single thread are Aurora's app limits. Cores are linked
into `RetroArch.bin`. You cannot drop in a `.so` core.

## Audio

Game Boy, the Sega 8-bit cores and CHIP-8 play 16-bit stereo PCM. The
frontend upsamples everything to 32768 Hz with a linear interpolator and a
lowpass, then plays it quieter than full scale, so square waves are not
a buzz. Samples loop on CSND voices 6 and 7. Fast-forward drops extra
batches so the sound does not fall behind.

NES uses agnes for gameplay plus a lightweight 2A03 APU for PCM sound.
DMC (sample) playback is incomplete, so a few titles may lack drums or
speech effects.
Master System and Game Gear audio is the PSG, generated at 22050 Hz and
played at 32768 Hz. FM stays off so a frame still fits.

## Speed

How fast a game runs depends on the game. Everything is on one 134 MHz
integer core, and the blit is nearest-neighbor into a 24-bit framebuffer.

* **Frameskip** 0–4 draws 1 frame out of (frameskip + 1). Skipped frames
  are still emulated.
* **Stretch** paints the full 400×240 every presented frame. **1x** and
  **Fit** paint fewer pixels when the picture is smaller than the screen.
* **Fast Fwd (hold R)**, once enabled, runs the core four times per
  refresh and skips drawing three of those. It is a speed-up, not a
  frame-time fix. R is also RetroPad R, so ColecoVision keypad 3 and
  CHIP-8 key B are held for the whole fast-forward.

## Saves

* `.srm` and `.stateN` names come from the ROM's base name. Two different
  games with the same file name share those files.
* NES battery RAM is not written to an `.srm`. Use save states.
* SMS Plus writes a 32 KB `.srm` for every game it loads, including
  SG-1000 and ColecoVision.
* Save states match one ROM and one build of this frontend. They do not
  load in mainline RetroArch. A struct-size or CRC mismatch is
  `Bad state file`.
* The state slot (0–9) resets to 0 on every load and is not saved in
  `retroarch.cfg`.

## Input and play

* One player, RetroPad port 0.
* No cheats, shaders, netplay, rewind, achievements, playlists, or
  softpatching.
* No zip or 7z loading.
* The file browser shows at most 256 directories and known ROMs.
* Game Boy is DMG only (Peanut-GB). No Game Boy Color and no Game Boy
  Advance.
* NES is iNES mappers 0, 1, 2, and 4 only (agnes 0.2.0).
* No Master System BIOS file is loaded. ColecoVision requires your own
  8192-byte `/Aurora/ROMs/BIOS.col` and only maps keypad keys 1, 2, 3, 4,
  `*`, and `#`.
* CHIP-8 does not map keys 0, 7, 9, E, or F. Programs larger than 3584
  bytes are rejected. Shift and load/store quirks are fixed as described
  on [Supported Systems](Supported-Systems.md).
* Light guns and other SMS peripherals are not mapped.
* The Master System sprite limit is enforced.

## Menu scope

The menu is Resume, Restart, save states, Load Content, four settings
(scale, frameskip, FPS, fast-forward), Information, and Quit. There is
no per-core option screen, no button remapper, and no region switch.
Master System region and PAL/NTSC follow the ROM header and the SMS Plus
game database.
