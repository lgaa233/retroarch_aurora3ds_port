# Limitations

This is RetroArch **0.1.7** for AuroraOS: a working libretro frontend with
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

Sound is **mono**. Aurora's CSND voices play at the same volume on both
speakers, so the cores' stereo is mixed down and played on one looping
voice (6). The frontend resamples each core's rate to the voice's real
32776 Hz with a linear interpolator, a DC blocker and a ~10 kHz lowpass.
Frames are paced by the screen (~59.8 Hz), so a core that runs at 60.1 Hz
(NES) plays about 0.5% lower, as in RetroArch. Dynamic rate control
(±0.5%) keeps about 20 ms of sound queued. A frame that comes late fades
to silence and fades back in instead of replaying stale sound; the
Information screen counts those underruns. Fast-forward drops extra
batches so the sound does not fall behind. **Volume** in Settings goes
from 0 to 125%.

NES uses agnes for gameplay plus a full integer 2A03 APU: both pulses with
sweep and envelope, triangle, noise, and DMC samples read from the ROM.
The APU's frame and DMC IRQ flags show in `$4015`, but the IRQs are not
raised on the CPU, so a game that times itself with them can misbehave.
Master System and Game Gear audio is the PSG, generated at 22050 Hz.
FM stays off so a frame still fits.

## Speed

How fast a game runs depends on the game. The cores run on the 134 MHz
ARM9. The nearest-neighbor scale into the 24-bit framebuffer runs as a job
on the ARM11 when the AuroraOS core is version 116 or later
(`AUDIO_CMD_RUN11`), which takes most of a frame's work off the ARM9. With
an older core it falls back to the ARM9, and Fit and Stretch are slower.
**Show FPS** prints which one is in use (`ARM11` or `ARM9`) and its time
per frame.

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

The menu is Resume, Restart, save states, Load Content, five settings
(scale, frameskip, FPS, fast-forward, volume), Information, and Quit. There is
no per-core option screen, no button remapper, and no region switch.
Master System region and PAL/NTSC follow the ROM header and the SMS Plus
game database.
