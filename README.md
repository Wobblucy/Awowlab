# AwowLab

Replay and analyse World of Warcraft combat logs. AwowLab reads a combat log or
a WarcraftLogs report and plays the fight back in 3D, with damage and healing
meters, deaths, cooldowns, raid frames, boss timers and annotations, and can
compare your play against a reference log.

Windows only. Pre-built releases are on the
[releases page](https://github.com/Wobblucy/WoWLogreplayer-Public/releases);
this file is about building it yourself.

## Building from source

Work down the list. Each step says how to check it worked.

```bat
git clone https://github.com/Wobblucy/Awowlab.git
cd Awowlab
```

### 1. Install the prerequisites

| What | Notes |
|------|-------|
| Windows 10 or 11, 64-bit | |
| Visual Studio 2022 | "Desktop development with C++" workload. It includes CMake 3.20+. |
| [Vulkan SDK](https://vulkan.lunarg.com/) | The installer sets `VULKAN_SDK`, which the build finds. |
| [vcpkg](https://github.com/microsoft/vcpkg) | Clone it anywhere and run its `bootstrap-vcpkg.bat`. |
| Python 3.9+ | Only for the setup check and the data tools. |

Tell the build where vcpkg is, once, then open a new terminal:

```bat
setx VCPKG_ROOT C:\path\to\vcpkg
```

### 2. Check your setup

```bat
python tools/check_setup.py
```

It lists what every step needs and marks anything missing. For a plain build,
only the prerequisites above matter.

### 3. Build

```bat
cmake --preset x64-debug
cmake --build out/build/x64-debug --config Debug
```

The first configure downloads and builds the C++ libraries listed in
`vcpkg.json`, which takes several minutes; later builds reuse them. The
`scripts/build-*.bat` files run the same commands.

If configure fails with a path longer than 260 characters, clone the repository
into a shorter folder such as `C:\src\AwowLab`.

The program is `out/build/x64-debug/AwowLab/Debug/AwowLab.exe`. Use the
`x64-release` preset for an optimised build.

### 4. Run the tests

```bat
ctest --test-dir out/build/x64-debug -C Debug
```

Every suite should pass. Tests that need a local combat log or live
WarcraftLogs access skip themselves.

### 5. Get the icons and maps

Spell icons, the full world map set and the boss arena maps are too large for
the repository. Either download the published set:

```bat
python tools/fetch-assets.py
```

or produce them from your own game install with the data tools (step 6).

### 6. Regenerate the game data (optional)

Everything under `AwowLab/data/` and `AwowLab/PNG/` is generated from the game
client by the scripts in `tools/`, and the generated files are committed, so
you only need this to update for a new game patch or to build without any
published files. `tools/README.md` lists the tools by stage, and
`docs/UPDATE-FLOW.md` is the step-by-step procedure, starting from a one-command
export of the game's data tables.

## Running your own fork

Everyone who builds AwowLab has their own `awowlab.config.json` at the
repository root. You don't need to make it: the first configure copies
`awowlab.config.example.json` to it and fills `settings_key` with a random
value, which encrypts your settings file. It is gitignored; never commit it.
`python tools/check_setup.py` confirms it is there and kept out of git.

Release builds update themselves from a GitHub releases page and sign in to
WarcraftLogs with a registered client id. The example points at the official
ones. If you publish builds of your own, fill in your own WarcraftLogs client
id, project URL, update and asset repositories, and the public half of your own
release signing key. `docs/BUILD-OPTIONS.md` explains each setting, and
`tools/release/SIGNING.md` how to make a signing key. Builds made with the
`x64-debug` preset never update themselves.

`python tools/release/release.py <version>` builds, packages and signs a
release, and with `--publish` uploads it to the repositories in that file.
`--dry-run` shows what it would do. `docs/UPDATE-FLOW.md` (Step 8) has the
details.

## Documentation

| File | About |
|------|-------|
| `docs/ARCHITECTURE.md` | How the code is organised and how a log becomes a replay |
| `docs/BUILD-OPTIONS.md` | Build settings: updater, update feed, WarcraftLogs client |
| `docs/UPDATE-FLOW.md` | Refreshing the game data for a new patch |
| `docs/CASC-EXTRACTION.md` | Getting files out of the game client |
| `tools/README.md` | What each tool does |
| `CONTRIBUTING.md` | Making changes, translating, and opening pull requests against `dev` |

## Licence

AwowLab is free software under the GNU Affero General Public License v3.0; see
`LICENSE`. Contributions are accepted under the `CLA.md` contributor agreement.
Third-party components and their licences are listed in
`THIRD-PARTY-NOTICES.md`.

World of Warcraft is a trademark of Blizzard Entertainment, Inc. AwowLab is not
made or endorsed by Blizzard.
