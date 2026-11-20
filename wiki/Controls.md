# Controls

One player only. Cores read RetroPad port 0. The New 3DS ZL and ZR
buttons and the C-stick are not visible to the Aurora SDK, so this app
cannot use them.

The circle pad and the D-pad are the same RetroPad directions. Either one
moves the menu cursor. They are digital: the circle pad is not an analog
stick here.

## In a game

| 3DS control | RetroPad | Notes |
|-------------|----------|-------|
| D-pad | D-pad | |
| Circle pad | D-pad | same directions as the D-pad |
| A | A | |
| B | B | |
| X | X | |
| Y | Y | |
| L | L | |
| R | R | also fast-forward, when that setting is on |
| START | Start | |
| SELECT | Select | |
| Touch the bottom screen | — | opens the menu |
| HOME | — | returns to the Aurora Home Menu; SRAM and `retroarch.cfg` are saved |

**Fast-forward** does nothing until Settings → **Fast Fwd (hold R)** is
On. Holding R then runs the core four times per screen refresh and does
not draw the three extra runs, which is about four times normal speed at
the 60 Hz refresh. R is still RetroPad R while you hold it, so on
ColecoVision that hold is also keypad **3**, and on CHIP-8 it is also key
**B**.

Per-system meaning of the face buttons is below. Game Boy uses the
obvious layout. Master System, Game Gear, SG-1000, and ColecoVision do
not.

## In the menu

| Control | Action |
|---------|--------|
| D-pad or circle pad up / down | move the cursor (up / down repeat when held) |
| A | choose the row; on a value, cycle it forward |
| B | back. On the main menu with a game running, B resumes. In a folder, B goes up. At `/`, B returns to the main menu |
| Left / Right | change State Slot, Video Scale, or Frameskip |
| L / R | in the file browser only: jump 16 rows |
| HOME | quit to the Aurora Home Menu |

The main menu, settings rows, and browser are described in
[Using the App](Using-the-App.md).

## Game Boy

Peanut-GB reads the Game Boy buttons only.

| 3DS | Game Boy |
|-----|----------|
| D-pad / circle pad | D-pad |
| A | A |
| B | B |
| START | Start |
| SELECT | Select |

L, R, X, and Y are not wired to the Game Boy.

## Master System, Game Gear, and SG-1000

SMS Plus maps the two-button pad like this:

| 3DS | Console |
|-----|---------|
| D-pad / circle pad | D-pad |
| B | Button 1 |
| A | Button 2 |
| START | **Start** on the Game Gear |
| START | **Pause** on the Master System and the SG-1000 |

X, Y, L, and SELECT are not console buttons on these three systems. R is
only the fast-forward hold.

Light guns, paddles, and the sports pad are not mapped, even if the core's
game database flags a title as using one.

## ColecoVision

The joystick and the two fire buttons stay on the D-pad, B, and A. The
ColecoVision keypad only has room for the keys most games ask for on the
title screen (1, 2, and sometimes 3, 4, `*`, and `#`):

| 3DS | ColecoVision |
|-----|----------------|
| D-pad / circle pad | joystick |
| B | fire button 1 |
| A | fire button 2 |
| X | keypad 1 |
| Y | keypad 2 |
| R | keypad 3 |
| L | keypad 4 |
| START | keypad `#` |
| SELECT | keypad `*` |

Keypad 0 and 5–9 are not mapped. If a game needs one of those keys, this
build cannot press it. Holding R for fast-forward also holds keypad 3.

The ColecoVision will not run the game until `/Aurora/ROMs/BIOS.col` is
present. See [Loading ROMs](Loading-ROMs.md).

## CHIP-8

The CHIP-8 keypad is 16 keys, `0`–`F`. This is the usual one-player
libretro arrangement. Keys **0, 7, 9, E, and F** have no button.

| 3DS | CHIP-8 key |
|-----|------------|
| X | 1 |
| Up | 2 |
| Y | 3 |
| Left | 4 |
| A | 5 |
| B | 6 |
| Down | 8 |
| L | A |
| R | B |
| START | C |
| SELECT | D |

Holding R for fast-forward also holds CHIP-8 key B. The sound timer still
counts down, but the app has nowhere to play the beep. See
[Limitations](Limitations.md).
