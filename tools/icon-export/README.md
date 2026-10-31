# icon-export

One command that exports the game's icon images from a local WoW install as the
PNG files the app loads. It replaces exporting `interface/icons` by hand in
CASC Explorer. This is part of Step 4 of `docs/UPDATE-FLOW.md`.

```bash
python tools/icon-export/export_icons.py
```

`dump_icons.js` reads each `interface/icons/*.blp` with wow.export's CASC code
(no window), decodes it with wow.export's BLP decoder and writes it as
`<name>.png`, lowercase, into `icon-pack/PNG/Spell Icons/` at the repository
root. That is the name `spell_icon_table.bin` and the culler use. Existing files
in the folder are overwritten, never deleted.

The icons are found by fileDataID. The list comes from the
`manifestinterfacedata` export that Step 1 (`tools/db2-export/export_db2.py`)
writes: the game's own record of its interface files, so it is always current
for the build. `--listfile <community-listfile.csv>` adds the icons the
community listfile names too (a few dozen more, mostly ones the manifest leaves
out), or replaces the manifest when there is no export. See
`docs/CASC-EXTRACTION.md` for where to get the listfile.

`--only-needed` exports just the icons `AwowLab/data/spell_icon_table.bin` and
`tools/icon-culler/required_icons.txt` name, about 14,000 instead of 33,000.
That is enough to run the app, but `cull_icon_pack.py` expects the full set.

## Needs

| Tool | Why | Where it looks |
|------|-----|----------------|
| Python 3.7+ | runs this script | |
| Node.js (tested with 24) | runs `dump_icons.js` | `node` on PATH, or `WOWEXPORT_NODE` |
| wow.export source checkout | its CASC reader and BLP decoder | `WOWEXPORT_SRC`, default `wowexport-src` beside this repository |
| `mmap.node` | wow.export's native file mapping addon | `WOWEXPORT_MMAP_NODE`, default `<checkout>/src/mmap.node` |
| `manifestinterfacedata` export | names the icons | `--db2-dir`, else `AWOWLAB_DB2_DIR`, else `rawdb2/<version>` |

The wow.export checkout is the same one `tools/db2-export` uses; see
`tools/external/wow.export/README.md`. Use node, not bun.

## Options

| Flag | Default | |
|------|---------|---|
| `--wow-dir` | `AWOWLAB_WOW_DIR`, else `C:/Program Files (x86)/World of Warcraft` | install root |
| `--product` | `wow` | `wow` is retail, `wowt` the PTR |
| `--out` | `icon-pack/PNG/Spell Icons` | PNG files |
| `--db2-dir` | `AWOWLAB_DB2_DIR`, else `rawdb2/<version>` | where `manifestinterfacedata.txt` is |
| `--listfile` | | community listfile, added to the manifest |
| `--only-needed`, `--icon-table` | `AwowLab/data/spell_icon_table.bin` | export only the icons the app uses |
| `--no-cdn` | | fail an icon rather than download it |

The exit code is 1 when an icon could not be exported.

## What to expect

On the 12.1.5 retail install, the full export (manifest plus listfile) wrote
33,133 icons in about a minute, all read from the install. A sample of 1,000
was pixel-identical to CASC Explorer's PNG export of the same files.

- **Icons not in this build** are skipped and counted. The listfile still names
  icons the game has removed, and a few that exist only for other locales.
- **Encrypted icons** (about ten, for content the client has not unlocked) are
  skipped and listed.
- **Some files may come from the CDN.** When the install lacks an icon,
  wow.export downloads it from Blizzard's CDN and keeps a copy under
  `%TEMP%\wowexport-headless`, and the summary names it. `--no-cdn` turns that
  off.
