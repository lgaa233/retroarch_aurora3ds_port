# Using the App

Boot AuroraOS, then open **RetroArch** from the Aurora Home Menu. The
title bar shows `RetroArch 0.1.2`.

The app opens straight into the menu, with the cursor on **Load Content**,
because nothing is running yet. After a game loads, the top screen is the
game and the bottom screen is a status page.

## The two screens

**While a game is running**

* Top screen (400×240): the game, nearest-neighbor scaled. An optional FPS
  counter sits in the top-left. Short messages (the on-screen display)
  sit on a black bar along the bottom for about two seconds.
* Bottom screen: the version, the ROM name, the core name and version,
  "Touch the screen for the menu", and "HOME quits to the Home Menu". If
  fast-forward is enabled, it also says "Hold R to fast-forward".

**While the menu is open**

* Top screen: the menu (dark background, green title).
* Bottom screen: button hints (`D-pad: move`, `A: choose`, `B: back`,
  `HOME: quit`), the list of cores and their extensions, and
  "ROMs go in SD:/Aurora/ROMs".

Touch the **bottom** screen during a game to open the menu. The top screen
is not a touch screen. Opening the menu clears the buttons so a press that
opened the menu is not also sent to the game.

## Main menu

| Entry | When it works | What it does |
|-------|---------------|--------------|
| Resume | a game is running | closes the menu |
| Restart | a game is running | resets the emulated machine, shows "Restarted", returns to the game |
| Save State | a game is running | writes the current slot; the result stays on the menu status line |
| Load State | a game is running | loads the current slot and returns to the game, or stays in the menu if it failed |
| State Slot | a game is running | slots **0–9**. Left / Right change it. A moves to the next slot |
| Load Content | always | opens the file browser |
| Settings | always | video scale, frameskip, FPS, fast-forward |
| Information | always | version, cores, console, paths |
| Quit RetroArch | always | leaves the app and returns to Aurora |

Resume, Restart, Save State, Load State, and State Slot are dimmed until a
game is running, and the cursor skips them. **B** on the main menu, while
a game is running, does the same thing as Resume.

Restart does not delete the `.srm` on the card. It resets the running
machine.

The full button list is on [Controls](Controls.md). Settings are on
[Settings](Settings.md). Saves are on
[Saves and Save States](Saves-and-Save-States.md).

## Load a game

1. Choose **Load Content**.
2. The browser opens the last folder you successfully loaded from, or
   `/Aurora/ROMs` if that folder is gone or you have not loaded anything
   yet.
3. A opens a `[folder]` or loads a ROM. B goes up a folder. B at `/`
   returns to the main menu.
4. A successful load closes the menu, remembers the folder, and shows the
   ROM name on the top screen for about two seconds.

If the core rejects the file, the menu stays open and the status line says
`Could not load <name>`. Common causes are on
[Troubleshooting](Troubleshooting.md). How the browser filters files is on
[Loading ROMs](Loading-ROMs.md).

Loading a new ROM unloads the previous one first. Unloading writes that
game's SRAM, if it has any.

## Information

The Information screen lists:

* `RetroArch 0.1.2 for AuroraOS` and that the cores are statically linked
* each core's name, version, and extensions (Peanut-GB `head`, agnes
  `0.2.0`, SMS Plus GX `head`, chip8 `1.0`)
* `New 3DS / New 2DS` or `Old 3DS / 2DS`
* `CPU: ARM9 @ 134 MHz (all models)`
* `Audio: stereo PCM (NES has none)`
* the live save directory and state directory

A or B returns to the main menu.

## Messages on the top screen

Yellow text on a black bar is the on-screen display. It lasts about two
seconds, then the borders are redrawn. You will see it for the ROM name
after a load, "Restarted", and a successful state load.

Save and load failures stay on the menu status line instead, so you can
read them while you are still in the menu. Those strings include
`Saved state to slot N`, `SD write failed`, `No state in slot N`, and
`Bad state file`.

## Leave

* **Quit RetroArch** unloads the game (SRAM is written), saves
  `retroarch.cfg`, and returns to the Aurora Home Menu.
* **HOME** also leaves RetroArch. An exit handler writes SRAM and
  `retroarch.cfg` on the way out, including when HOME exits the process.

There is one game at a time, and only player 1.
