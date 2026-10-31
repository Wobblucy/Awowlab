# Third-Party Notices

AwowLab is licensed under the GNU Affero General Public License v3.0 (see
`LICENSE`). It is built with the components below, each under its own licence.
Exact versions are pinned in `vcpkg.json` and the `FetchContent` blocks of the
CMake files.

## Included in this repository

| Component | Where | Licence | Copyright |
|-----------|-------|---------|-----------|
| ImPlot | `AwowLab/UIManager/third_party/implot/` | MIT | Copyright (c) 2020 Evan Pezent |
| stb_image | `AwowLab/UIManager/include/stb_image.h` | MIT or public domain, at your choice | Sean Barrett |
| miniz 3.0.0 | `AwowLab/Updater/miniz.c`, `miniz.h` | MIT (parts released into the public domain) | Copyright 2013-2014 RAD Game Tools and Valve Software; Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC |
| Noto Sans | `AwowLab/data/fonts/NotoSans-Regular.ttf` | SIL Open Font License 1.1 (`AwowLab/data/fonts/OFL.txt`) | Copyright 2012 Google Inc. |

## Downloaded at build time

| Component | How | Licence | Copyright |
|-----------|-----|---------|-----------|
| Dear ImGui | CMake FetchContent | MIT | Copyright (c) 2014-2024 Omar Cornut |
| GLFW | CMake FetchContent | zlib | Copyright (c) 2002-2006 Marcus Geelnard; Copyright (c) 2006-2019 Camilla Löwy |
| tinyobjloader | CMake FetchContent | MIT | Copyright (c) 2012-2019 Syoyo Fujita and many contributors |
| portable-file-dialogs | CMake FetchContent | WTFPL v2 | Copyright (C) 2004 Sam Hocevar |
| Catch2 (tests only) | CMake FetchContent | Boost Software License 1.0 | Catch2 authors |
| Boost (Beast, Asio and their dependencies) | vcpkg | Boost Software License 1.0 | Boost authors |
| OpenSSL | vcpkg | Apache License 2.0 | The OpenSSL Project Authors |
| zlib | vcpkg | zlib | (C) 1995-2022 Jean-loup Gailly and Mark Adler |
| RapidJSON | vcpkg | MIT | Copyright (C) 2015 THL A29 Limited, a Tencent company, and Milo Yip |
| Vulkan SDK headers and loader | installed separately | Apache License 2.0 | The Khronos Group Inc. |

Each component's full licence text ships with its source: in the folder named
above, in the FetchContent download under the build tree, or under
`share/<package>/copyright` in the vcpkg install.

## Data and release tools

None of the following is part of the program. They are only needed to
regenerate game data or publish a release with the scripts under `tools/`.

Code from other projects kept in this repository, under its own licence:

| What | Where | From | Licence |
|------|-------|------|---------|
| Patches and headless scripts for wow.export | `tools/external/wow.export/` | [Kruithne/wow.export](https://github.com/Kruithne/wow.export) | MIT (`tools/external/wow.export/LICENSE`); Copyright (c) Kruithne, Copyright (c) Marlamin |
| Patch and batch exporter for db2 | `tools/external/db2reader/` | [erorus/db2](https://github.com/erorus/db2) | Apache License 2.0 (`tools/external/db2reader/LICENSE`) |

Programs the scripts run, which you install yourself (not included):

| Program | Used by | Licence |
|---------|---------|---------|
| [wow.export](https://github.com/Kruithne/wow.export), with its Blender importer | Models, dungeon geometry, DB2 tables | MIT |
| [db2](https://github.com/erorus/db2) on PHP | Converting `.db2` files to text | Apache License 2.0 (PHP: PHP License 3.01) |
| [rustydemon](https://github.com/HoldMyBeer-gg/rustydemon) | World map tiles (`tools/worldmap-builder/`) | GNU AGPL v3 with the "Commons Clause" condition, which withholds the right to sell it |
| [CASC Explorer](https://github.com/WoW-Tools/CASCExplorer) | Optional, for tiles or icons by hand | See its repository |
| [Blender](https://www.blender.org/) | Arena map renders (`tools/arena-maps/`) | GNU GPL v3 |
| Node.js, Python | Running the scripts | MIT-style, PSF License |
| Python packages in `tools/requirements.txt`: Pillow, cryptography, deepl | Image work, release signing, translations | MIT-CMU, Apache 2.0 or BSD, MIT |

**rustydemon and its licence.** rustydemon's licence adds the Commons Clause
to the AGPL, which withholds the right to sell rustydemon or a product that
derives its value from it. We considered what that means for AwowLab:

- No rustydemon code is in this repository or in any AwowLab build. It is not
  linked, bundled, patched or redistributed; `extract_season_tiles.sh` runs a
  copy you install yourself as a separate program.
- What it produces for us is Blizzard's own map art, read out of the game
  install. The tiles are game data (see below), not rustydemon output in any
  creative sense, and the app works the same whichever tool extracted them.
- The step does not depend on it: CASC Explorer extracts the same tiles
  (`docs/CASC-EXTRACTION.md`), and anyone preparing a build for sale can use
  that instead.

On that reading rustydemon's terms do not extend to AwowLab or to its
licensing, including the commercial option in `CLA.md`.

The DeepL and WarcraftLogs services used by the translation and report tools
have their own terms; you need your own account for each.

## World of Warcraft data

World of Warcraft and its names, images and game data are trademarks and
copyrighted works of Blizzard Entertainment, Inc. AwowLab is not made or
endorsed by Blizzard. The data files generated from the game client (spell data
and names, icons, maps, models) are produced by the scripts under `tools/` for
use with the game and are not covered by AwowLab's licence.
