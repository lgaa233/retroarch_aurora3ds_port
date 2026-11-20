# Settings

Open the menu (touch the bottom screen), then choose **Settings**.
Changes apply immediately. They are written to `retroarch.cfg` when you
leave Settings with **B** or with **A** on **Back**, and again when the
app exits.

Left cycles a value backward. Right and A cycle it forward. Up and Down
move between rows.

| Row on screen | What it does | Default |
|---------------|--------------|---------|
| Video Scale | `1x (centered)`, `Fit (keep aspect)`, or `Stretch (400x240)` | Fit |
| Frameskip | `0` through `4` | 0 |
| Show FPS | `On` / `Off` | Off |
| Fast Fwd (hold R) | `On` / `Off`. Hold R in game to use it | Off |
| Back | saves and returns to the main menu | |

## Video scale

The top screen is 400×240. Scaling is nearest-neighbor, done with integer
math, and the picture is centered. Black borders fill whatever the
picture does not cover. "Keep aspect" means the width and height of the
frame the core just submitted. The blitter does not apply a separate
television pixel-aspect ratio.

| Mode | `scale` in the cfg | Behavior |
|------|--------------------|----------|
| 1x (centered) | `0` | one source pixel per screen pixel, centered. Every core in 0.1.1 fits in 400×240, so this is the native picture. A frame larger than the screen on either axis would be scaled down to that edge |
| Fit (keep aspect) | `1` | the largest size that still fits in 400×240. Integer division can be a pixel off a perfect ratio |
| Stretch (400x240) | `2` | always the full top screen, so the picture is stretched |

Examples of Fit:

* Game Boy and the usual Game Gear picture are 160×144, which Fit draws
  at 266×240.
* A 256×240 NES picture already matches the screen height, so 1x and Fit
  are the same 256×240 image. Stretch widens it to 400×240.
* A 256×192 Master System picture Fits at 320×240.
* CHIP-8 is 64×32. 1x is a small block in the center. Fit is 400×200.

Stretch fills more pixels, so it costs more blit time than 1x. If a game
is missing frames, 1x or Fit plus a frameskip is the lighter combination.
See [Limitations](Limitations.md).

## Frameskip

Frameskip `N` draws 1 frame out of `N + 1`. The core still runs the
skipped frames; they are not copied to the screen.

| Value | Drawn |
|-------|-------|
| 0 | every frame |
| 1 | 1 of 2 |
| 2 | 1 of 3 |
| 3 | 1 of 4 |
| 4 | 1 of 5 |

Values outside 0–4 in the config file are put back to 0 on load.

## Show FPS

When this is On, a green counter is drawn in the top-left of the top
screen (`123 fps`). The number is how many times the main loop ran the
game during the last second. The three extra core runs performed while
fast-forwarding are not added to it, so the counter can stay near the
refresh rate while the game is actually advancing faster.

## Fast-forward

**Fast Fwd (hold R)** only arms the feature. In the game, hold **R**.
Each refresh then runs the core once (that run can still be frameskipped)
plus three more runs whose pictures are dropped. At the 60 Hz refresh
that is about four times normal speed.

R remains the RetroPad R button for those frames. On ColecoVision that is
keypad 3. On CHIP-8 that is key B. Other cores in this build do not read
R as a game button.

## `retroarch.cfg`

The file is `<app dir>/retroarch.cfg`. For the standard install:

```text
SD:/Aurora/Apps/C/RetroArch/retroarch.cfg
```

It is plain text, one `key = value` per line:

```text
scale = 1
frameskip = 0
show_fps = 0
fast_forward = 0
last_dir = /Aurora/ROMs/gb
```

| Key | Values |
|-----|--------|
| `scale` | `0` 1x, `1` Fit, `2` Stretch. Anything else loads as Fit |
| `frameskip` | `0`–`4`. Anything else loads as `0` |
| `show_fps` | `0` off, any other number on |
| `fast_forward` | `0` off, any other number on |
| `last_dir` | Aurora path reopened by **Load Content**. Empty until the first successful load. Paths look like `/Aurora/ROMs`, not `SD:/…` |

Unknown lines are ignored. `last_dir` is updated when a ROM loads
successfully, not when you merely browse. If that directory is gone, the
browser falls back to `/Aurora/ROMs`.

Editing the file on a PC is fine while RetroArch is not running. The app
rewrites the whole file on exit, so in-game changes replace hand edits
from that session.
