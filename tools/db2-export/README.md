# db2-export

One command that turns a local WoW install into the DB2 text exports every
script under `tools/` reads. It replaces exporting `dbfilesclient/` by hand in
CASC Explorer. This is Step 1 of `docs/UPDATE-FLOW.md`.

```bash
python tools/db2-export/export_db2.py --out "<exports>"
```

It runs three steps and prints a summary:

1. `dump_db2.js` copies the raw `.db2` files out of the install with
   wow.export's CASC code (no window), into `<exports>/db2/`.
2. db2reader's `export_batch.php` converts them to `{table}.txt` in `<exports>`.
3. Each table's `Layout:` hash is checked against `tools/db2_layouts.json`.

By default it exports only the tables the tools read, listed with their
fileDataIDs in `tables.csv`. Add a line there when a script starts reading a new
table. `--all` exports every table instead, which needs the community listfile
to name them (`--listfile`, see `docs/CASC-EXTRACTION.md`) and takes several
minutes to convert.

## Needs

| Tool | Why | Where it looks |
|------|-----|----------------|
| Python 3.7+ | runs this script | |
| Node.js (tested with 24) | runs `dump_db2.js` | `node` on PATH, or `WOWEXPORT_NODE` |
| wow.export source checkout | its CASC reader | `WOWEXPORT_SRC`, default `wowexport-src` beside this repository |
| `mmap.node` | wow.export's native file mapping addon | `WOWEXPORT_MMAP_NODE`, default `<checkout>/src/mmap.node` |
| PHP 8.3+ | runs db2reader | `php` on PATH, or `PHP` |
| db2reader checkout | the converter | `AWOWLAB_DB2READER`, default `db2reader` beside this repository |

Set up the wow.export checkout as `tools/external/wow.export/README.md`
describes and run `bun install` in it; the raw dump only uses code that is the
same with or without our patches. `mmap.node` ships beside `wow.export.exe` in
an installed copy of wow.export. Set up db2reader from
`tools/external/db2reader/README.md`, including copying `export_batch.php` in.

Use node, not bun, for `dump_db2.js`: bun 1.3 fails to load wow.export's CASC
modules.

## Options

| Flag | Default | |
|------|---------|---|
| `--wow-dir` | `AWOWLAB_WOW_DIR`, else `C:/Program Files (x86)/World of Warcraft` | install root |
| `--product` | `wow` | `wow` is retail, `wowt` the PTR. The script lists what is installed if you pick one that is not |
| `--out` | `AWOWLAB_DB2_DIR`, else `rawdb2/<version>` | text exports |
| `--raw-dir` | `<out>/db2` | raw `.db2` files. Cleared of `.db2` files at the start of each run |
| `--version` | `DEFAULT_VERSION` in `tools/db2_layouts.py` | recorded layout to check against |
| `--all`, `--listfile` | | every table, named by the listfile |
| `--no-cdn` | | fail a table rather than download it |

The exit code is 1 when a table could not be extracted or converted. A changed
layout is reported but does not fail the run, since a new patch is expected to
change some; Step 2 of the update flow covers what to do with it.

## What to expect

- **Encrypted rows are left blank.** Several tables (`spell`, `spellname`,
  `spelleffect` and others) carry blocks encrypted for content the client has
  not unlocked. Those blocks are written as zeros, which is what CASC Explorer
  does. The summary lists the affected tables.
- **Some files come from the CDN.** A Battle.net install does not always hold
  every file locally. When a table cannot be read from the install, wow.export
  downloads it from Blizzard's CDN and keeps a copy under
  `%TEMP%\wowexport-headless`, and the summary names it. `--no-cdn` turns that
  off. On the 12.1.5 retail install, `spell` and `spellname` came from the CDN.
- **Tables retired from the game** are still in the listfile. With `--all` they
  are counted as not in this build and skipped.
- **db2reader cannot convert three sparse tables**
  (`collectablesource*sparse`); none of them are read by the tools.

When a table will not extract at all, export it from CASC Explorer instead and
convert that file with db2reader. Different CASC readers disagree about which
files they can read; see `docs/CASC-EXTRACTION.md`.
