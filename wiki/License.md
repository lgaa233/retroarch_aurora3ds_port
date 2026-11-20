# License

RetroArch for AuroraOS is released under the **GNU General Public
License, version 3** (GPL-3.0). The full text is the `LICENSE` file in
the repository root.

The frontend is GPL-3.0 in the same spirit as RetroArch and AuroraOS: an
app built with the Aurora SDK links GPL code from the OS. If you
distribute a binary you built, the GPL-3.0 terms apply to that
combination, including the corresponding source.

This is an unofficial frontend. It is not published by the RetroArch
team or by Team AuroraOS.

## Vendored code

These pieces keep their own licenses. Their headers in the tree are the
authoritative text.

| Path | Project | License |
|------|---------|---------|
| `third_party/libretro/libretro.h` | libretro API header | MIT. Copyright (C) 2010-2024 The RetroArch team. The MIT grant applies to that header only |
| `third_party/peanut_gb/peanut_gb.h` | [Peanut-GB](https://github.com/deltabeard/Peanut-GB) | MIT. Copyright (c) 2018-2023 Mahyar Koshkouei. The header also notes code taken from SameBoy |
| `third_party/agnes/` | [agnes](https://github.com/kgabis/agnes) | MIT. Copyright (c) 2019 Krzysztof Gabis (the `LICENSE` file); `agnes.h` also says Copyright (c) 2022 Krzysztof Gabis |
| `third_party/smsplus/` | [SMS Plus GX](https://github.com/libretro/smsplus-gx) | GPL-2.0 or later. Copyright (C) 1998-2007 Charles MacDonald, with additional code by Eke-Eke and gameblabla. See the headers in that directory |

`compat/dirent.h`, the frontend sources under `src/`, and the host tests
are part of this project and are covered by GPL-3.0.

AuroraOS is cloned at build time into `third_party/AuroraOS` from
<https://github.com/TeamAuroraOS/AuroraOS> at the commit pinned in the
Makefile. It is not vendored in this repository and remains under the
license in that project.

## Save data and games

Nothing in this license grants a right to commercial ROM images or to a
console BIOS. `BIOS.col` and cartridge dumps have to be files you made
from hardware you own. CHIP-8 programs are typically public domain; the
interpreter that runs them is still the code licensed above.
