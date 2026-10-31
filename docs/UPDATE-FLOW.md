# Game Version Update Flow

What to run, in what order, when WoW ships a new patch and AwowLab needs to follow.

The short version: convert the DB2 files to text, find out which tables changed
shape, fix those, then rerun the generators and copy their output into
`AwowLab/data/` and `AwowLab/PNG/`.

## The one thing that will bite you

The text exports are **positional dumps**. Line 1 is `Layout: <hash>`, every line
after is `<id>: field,field,field,...` with **no column names**. Every tool reads
fields by index.

When Blizzard adds or removes a column, each index after it shifts. The file still
parses. The field count often does not even change. You get plausible, wrong
numbers and no error.

`Layout:` is the structure fingerprint that makes this detectable, so
`tools/db2_layouts.json` records the known-good hash per table per version, and
`python tools/db2_layouts.py --check <exports> --version <v>` compares a whole
export folder against it. The spell data and talent scripts also check the tables
they read; the map, icon table, spell name and encounter scripts do not yet, so run
the folder check before them. **A layout check that fails is doing its job - never
work around it.**

A changed hash means *inspect*, not *remap*. In 12.1, `spellclassoptions` changed
hash but its columns did not move; `skilllineability` genuinely dropped its leading
`RaceMask` field and slid everything left by one. Diff real rows before you touch
a parser.

## Prerequisites

Run `python tools/check_setup.py` first. It lists, step by step, which of the
programs, packages and settings below are present on your machine and which are
missing, and checks your DB2 exports against the recorded layouts. It changes
nothing.

- **Python 3.9+**, with `python -m pip install -r tools/requirements.txt`
- **PHP 8.3+** - for the DB2 converter
- **db2reader** - a separate project (erorus/db2) that converts raw `.db2` into
  text exports. Set it up from `tools/external/db2reader/README.md`, which
  holds the one patch and the batch script we use with it.
- **pngquant** - only for the icon and map compression steps, from
  https://pngquant.org/ . On PATH is simplest; the map stitcher also looks in
  `tools/worldmap-builder/pngquant/` and the icon scripts in `tools/worldmap-builder/`.
  Version 2.17.0 reproduces the shipped map images byte for byte.
- A World of Warcraft install carrying the patch you are building for (retail or
  PTR). The export script in Step 1 reads it directly.

## Step 1 - Export the DB2 tables as text

Nothing in this repo reads raw `.db2`. One script pulls the tables out of the
game install, converts them with db2reader and checks their layouts:

```bash
# wow is retail, wowt the PTR; check .build.info for which one has the new patch
python tools/db2-export/export_db2.py --product wowt --out "<exports>"
```

It needs node, PHP, a wow.export checkout and a db2reader checkout; see
`tools/db2-export/README.md` for where it looks for each. By default it exports
only the tables below (from `tools/db2-export/tables.csv`); `--all` exports every
table. The summary names any table taken from the CDN, any with encrypted rows
left blank, and any whose layout no longer matches `tools/db2_layouts.json`.

Put the result somewhere stable. Every tool finds the exports the same way: the
folder given on its command line, else the `AWOWLAB_DB2_DIR` environment
variable, else `rawdb2/<version>` at the repository root. Setting
`AWOWLAB_DB2_DIR` once lets you leave `"<exports>"` off the commands below.

Either naming convention works (`{name}.txt` or `{name}_export.txt`) - the tools
resolve both through `find_export()`. The current exporter produces plain `.txt`.

### When a table will not extract

Export that file from `dbfilesclient/` in CASC Explorer instead
(`docs/CASC-EXTRACTION.md` covers the listfile and product), then convert it from
the db2reader checkout:

```bash
php example.php <dump>/uimap.db2 > <exports>/uimap.txt

# or a whole folder, each written as {table}.txt
php export_batch.php <dump-folder> <exports-folder> --keep-name
```

Pass **forward-slash** paths when calling php from bash; backslashes eat shell
variables. CASC readers disagree about which files they can read, so a file one
tool fails on is often fine in another.

### Tables that matter

Only 30 of the ~1100 tables are used:

| Purpose | Tables |
|---|---|
| Maps | `uimap`, `uimapxmapart`, `uimaparttile`, `manifestinterfacedata`, `uimapassignment` |
| Spells | `spellname`, `spellcooldowns`, `spellcasttimes`, `spellmisc`, `skilllineability`, `spellcategory`, `spellcategories`, `spellclassoptions`, `spelleffect`, `spelllabel`, `spellpower`, `spell`, `spellauraoptions` |
| Items | `itemeffect` |
| Talents | `traitdefinition`, `traitnodeentry`, `traitsubtree` |
| Encounters | `dungeonencounter`, `journalencounter`, `journalencountersection`, `journalinstance` |
| Models | `creature`, `creaturedisplayinfo`, `creaturemodeldata`, `journalencountercreature` |

## Step 2 - Find out what changed

Before running anything else, record the new version's hashes and diff them:

```bash
# Print a block of the new dump's hashes
python tools/db2_layouts.py --scan "<exports>" --version 12.2

# Paste that into tools/db2_layouts.json under "versions", then:
python tools/db2_layouts.py --diff 12.1 12.2
```

That prints exactly which tables changed shape. **This is the scope of your work.**
In the 12.0 -> 12.1 bump it was 2 of 23, and only one needed a code change.

For each changed table, dump a row from the old and new exports side by side and
compare field by field. If the columns moved, fix the parser and add the new hash
to its column map - see `SKILLLINEABILITY_COLUMNS` in
`tools/spell-data-generator/extract_spell_gcd.py` for the pattern, and its test
beside it.

Then bump `DEFAULT_VERSION` in `tools/db2_layouts.py`. That one edit renames the
generated files and selects which layout the tools check against.

## Step 3 - Generate spell data

Order matters here: `db2_extractor` needs `spell-data-generator`'s output.

```bash
# 1. Per-class spell lists - the primary GCD source
python tools/spell-data-generator/extract_spell_gcd.py "<exports>"
#    -> tools/spell-data-generator/output/{class}_spells.csv

# 2. The binary the app loads
python tools/db2_extractor/build_cooldown_tables.py "<exports>" \
       tools/db2_extractor/output tools/spell-data-generator/output
#    -> output/spell_data_{version}.bin        <- copy to AwowLab/data/

# 3. WCL talent ids folded into the modifier file (edits AwowLab/data/ in place)
python tools/db2_extractor/build_talent_mapping.py "<exports>"
```

Steps 2 and 3 target `DEFAULT_VERSION`; add `--version <v>` to regenerate an
older version's files.

`cooldown_modifiers_{version}.json` is hand-maintained: its flat, resource and
charge entries are edited by hand and carried forward, and step 3 only adds the
WarcraftLogs talent ids. Copy the previous version's file to the new version's
name before running, or it will not find one. `charge_spells.json` is hand-written
too; no script produces it.

## Step 3b - Haste buffs

```bash
python tools/db2_extractor/build_haste_buffs.py "<exports>"
#    -> AwowLab/data/haste_buffs_{version}.json
```

Every aura a player can gain that changes haste by a percentage (Bloodlust, Icy
Veins, Berserking...), with its value and how far it stacks. The haste tracker
reads it to work out each cast's global cooldown. Haste rating from gear comes
from COMBATANT_INFO and is not in this file.

A buff here is a spell effect applying aura 193, kept when a player can come by
the spell: a class spell, a skill line, a talent, an item, or something one of
those triggers. The script's header explains the rule. It prints the buffs it
left out because their value is set by a server script rather than the spell
data; those show no haste. A few buffs are granted by a script with nothing in
the data linking them to a player at all, and `SCRIPT_GRANTED` in the script
names them. If a pair there stops matching, the run stops and says which.

`--version <v>` regenerates an older version's file.

## Step 4 - Spell names and icons

```bash
# Spell names (en_US; --locale for others). Reads spellname from the export
# folder, or pass the file.
python tools/spell-name-converter/convert_spellnames.py
#    -> AwowLab/data/spellnames/spell_names.csv.gz

# Icon table -> binary
python tools/spell-icon-generator/generate_spell_icon_table.py "<exports>"
python tools/spell-icon-generator/generate_spell_icon_binary.py
#    -> AwowLab/data/spell_icon_table.bin
```

The icon images come from the game client:

1. Export them: `python tools/icon-export/export_icons.py` writes every
   `interface/icons` texture as a lowercase PNG into `icon-pack/PNG/Spell Icons/`
   at the repository root, finding them through the `manifestinterfacedata`
   export from Step 1. It needs node, the wow.export checkout and `mmap.node`,
   as Step 1 does; see `tools/icon-export/README.md`. Add
   `--listfile <community-listfile.csv>` to pick up the few icons the manifest
   leaves out.
   If it cannot read some icons, export `interface/icons` as PNG with CASC
   Explorer instead (`docs/CASC-EXTRACTION.md`) into the same folder.
2. Compress them: `python tools/spell-icon-generator/compress_icons.py`
   writes `icon-pack/PNG/Spell Icons - Compressed/`.
3. Keep only the icons the app can show:
   `python tools/icon-culler/cull_icon_pack.py` reads the compressed set and
   writes the kept icons to `tools/icon-culler/output/`. Copy them into
   `AwowLab/PNG/Spell Icons/`.

All three take `--out` (and the last two `--source`) to work on other folders.

`tools/icon-culler/build_icon_pack.py` packages a culled set into the chunked
archives a release publishes. A source build that does not need to regenerate the
icons can download the published set with `python tools/fetch-assets.py`.

## Step 5 - Maps

```bash
# 1. Every map's bounds and tile layout
python tools/worldmap-builder/generate_master_coordinates.py "<exports>"
#    -> master_map_alignment_coordinates.json (~18 MB, not committed)

# 2. The CSV the app reads
python tools/worldmap-builder/extract_map_bounds.py
#    -> AwowLab/PNG/Worldmaps/map_world_bounds.csv
```

`extract_map_bounds.py` writes straight to the canonical location - there is no
copy step. A map id missing from that CSV renders as a blank grid floor.

**The app writes to this file too.** It stores rate-limit state in a row with a
reserved map id (99999) that no real map uses, as one of three redundant locations
(see `Core/Settings/RateLimitBackup.h`). The generator preserves any row at or
above `RESERVED_MAP_ID_FLOOR` across a rebuild. In debug builds CMake junctions
`PNG/` to the source tree, so that row appears as an uncommitted one-line diff in
your working copy - that is runtime state, not a change to commit.

### Map images

The bounds CSV only positions a map; the image itself is stitched from BLP tiles
extracted from the client. A map id with bounds but no PNG renders as a blank
grid floor.

```bash
# 1. Which tiles does this season need? (uimap ids, comma separated)
#    --listfile writes the fileDataID list that step 2 extracts.
python tools/worldmap-builder/list_required_files.py "2433,2500,2513,..." \
       --listfile tools/worldmap-builder/season_<version>_listfile.csv

# 2. Extract them - one batch call; see the script header for the product trap.
#    AWOWLAB_WOW_DIR overrides the default install root, AWOWLAB_LISTFILE the
#    listfile, RUSTYDEMON_CLI the extractor.
./tools/worldmap-builder/extract_season_tiles.sh wowt     # -> tiles_blp/

# 3. BLP -> PNG (Pillow decodes BLP directly, no external converter)
python tools/worldmap-builder/convert_tiles_to_png.py     # tiles_blp/ -> tiles/

# 4. Stitch. It asks for the tiles folder (defaulting to tiles/) and the
#    pngquant quality (default 65-80, which matches the shipped maps).
python tools/worldmap-builder/stitch_map_tiles.py         # -> output/{uimapId}.png
```

Copy only the PNGs from `output/`. The stitcher also writes its own
`map_world_bounds.csv` there, without the reserved row described above; the one in
`AwowLab/PNG/Worldmaps/` comes from `extract_map_bounds.py` and must not be
overwritten.

**Only install maps whose tiles are all present.** The stitcher happily builds a
partial map, and the result has blank holes where tiles were missing - worse than
no map at all. Check each map is `12/12` before copying its PNG into
`AwowLab/PNG/Worldmaps/`.

For getting the tiles out of the client in the first place — updating the
listfile, picking the right product, what to do when a file will not extract —
see [CASC-EXTRACTION.md](CASC-EXTRACTION.md).

Extraction tools disagree about which files they can read from the same build:
during 12.1, rustydemon 404'd on 87 tiles that CASC Explorer extracted fine, and
merging both extracts was what completed the set. If a tile appears to be missing,
try the other tool before concluding it was never shipped. CASC Explorer also
needs a current listfile from https://github.com/wowdev/wow-listfile - a stale one
hides whole folders whose names it does not yet know.

For new map tiles, see [tools/worldmap-builder/howtobuildWorldMaps.md](../tools/worldmap-builder/howtobuildWorldMaps.md).

## Step 5b - Hero talents

WarcraftLogs lists every talent a character took as trait node entry ids on its
combatantinfo event, but never says which of them are hero talents -
`customPowerSet` and the fields beside it come through empty. Hero choice
changes rotation, buffs and cooldown usage, so an imported fight without it
compares two different playstyles as if they were one.

`traitsubtree` supplies the names, and `traitnodeentry.TraitSubTreeID` says
which entries belong to one:

```
WCL talentTree[].id -> traitnodeentry.TraitSubTreeID -> traitsubtree.Name
```

```bash
python tools/db2_extractor/build_hero_talents.py <export_dir> --verify out/build/x64-debug/AwowLab/Debug/wcl_cache
```

Writes `AwowLab/data/hero_talents_{version}.json`, which ships inside data.zip.

Pass `--verify` whenever there are cached reports. A character has exactly one
hero tree, so a build resolving to two means the mapping is wrong - and that is
not visible from the DB2 tables alone, only from real characters. The 12.1 run
resolved 219 of 219 builds with none ambiguous.

Which hero trees each spec can choose is not in the client data; it lives in a
hand-kept table, `tools/db2_extractor/hero_talent_specs.py`. A patch that adds a
hero tree needs a line there first, or the script stops and names the tree it does
not know (12.1's Void-Scarred is the example).

Two traps in these tables:

- Subtree descriptions contain commas, so the numeric columns cannot be found by
  counting from the left. Read them from the end of the row.
- The table ships UI test rows marked `[DNT]`; they are not hero trees.

## Step 6 - Encounters

```bash
python tools/generate-encounter-id/extract_encounters.py "<exports>/dungeonencounter.txt"
#    -> output/encounter_names.{csv,bin}
```

A reference table for naming things by encounter id; nothing in the app reads it
and it is not shipped. Encounter geometry reaches the app through **filenames**
(`encounter_{id}_x{cx}_y{cy}_s{scale}.png`), parsed by
`EncounterTextureManager.cpp`. There is no encounter bounds table.

## Step 6b - Boss arena maps

Top-down renders of each boss room, made in Blender from wow.export's geometry.
The whole flow, from export to the compressed images, is in
`tools/arena-maps/README.md`.

## Step 6c - Enemy models

The creature model pack, `season_models_{version}.awmp`: export the listed models
with wow.export and bake them. See `tools/model-extractor/SEASON_MODELS_README.md`.
The bake currently writes `season_models_12.1.awmp`; pass `--out` for another
version.

## Step 7 - Verify

Regeneration is part of every release, not an occasional chore: at one point the
shipped `spell_data_12.0.bin` was 10 records behind what its own exports produced,
with 85 spells carrying stale cooldowns.

- [ ] `python tools/db2_layouts.py --check "<exports>" --version <new>` passes
- [ ] `python tools/spell-data-generator/test_skilllineability_columns.py` passes
- [ ] `python tools/db2_extractor/test_talent_mapping_preserves.py` passes
- [ ] `python tools/db2_extractor/test_haste_buffs.py` passes
- [ ] `haste_buffs_{version}.json` still has Bloodlust (2825) at 300, and its buff
      count is close to the previous version's (262 for 12.1)
- [ ] Regenerated `spell_data_{version}.bin` record count is sane (~35k) and close
      to the previous version's
- [ ] Spot-check a few known spells' cooldowns against Wowhead. A column shift
      corrupts thousands of rows uniformly; real balance drift touches a handful
      in plausible directions
- [ ] `map_world_bounds.csv` still has its reserved row, and a map loads in-app
      rather than rendering as a blank grid
- [ ] The app runs with `spellDataVersion` set to the new version

The runtime picks the version from the `spellDataVersion` setting
(`Core/UnifiedSettings.h`) and builds filenames by concatenation, so a version with
no matching files in `AwowLab/data/` **degrades silently** - no cooldowns, no error.

## Step 8 - Publish

`tools/release/release.py` does the whole release. It needs the private signing
key, because the updater refuses unsigned releases (`tools/release/SIGNING.md`),
and it uploads nothing until you ask it to in a second run.

```bash
# Bump AwowLab/Core/Version.h first; release.py refuses a version that differs.
set AWOWLAB_SIGNING_KEY=C:\path\to\awowlab-signing-key.pem

# See what it would do. Writes nothing.
python tools/release/release.py {VERSION} --dry-run

# Build, stage release/, write and sign the manifest, check the folder.
python tools/release/release.py {VERSION}

# Upload release/ to update_repo in awowlab.config.json.
python tools/release/release.py {VERSION} --publish --notes-file notes.md
```

`--skip-build` reuses an existing build. `--offline` skips the read-only check
that every static asset is published on the assets tag. `--stage-only` stops
before signing, for checking the packaging without the key; that folder cannot
be published.

The app release carries the executables, `shaders.zip` and `data.zip`. Maps,
icons, triggers and demo replays sit on a separate assets tag, recorded in
`tools/release/assets-hashes.json`; they change rarely and are large. When they
change, put the complete set in one folder (never in `release/`) and cut a new
tag rather than editing the old one - manifests published earlier still point at
it:

```bash
python tools/release/release.py {VERSION} --assets-tag assets-v{N} --assets-dir release-assets
python tools/release/release.py {VERSION} --publish --notes-file notes.md \
    --assets-tag assets-v{N} --assets-dir release-assets
```

The first run rewrites `assets-hashes.json` for the new tag (commit it); it
refuses if any archive, icon chunk or language pack the file lists is missing
from the folder, or if `Worldmaps.zip` lacks `map_world_bounds.csv`. The publish
run creates the assets release before the app release.
`tools/release/generate-assets-hashes.py` is the same step on its own.

**Carry every archive forward.** An assets tag is a complete set, not a delta - the
updater fetches all of them from whichever tag the manifest names. `langpack_ko_KR.zip`
was left behind on assets-v3 and three later manifests pointed at a tag that did not
have it, so Korean users got a 404 for two releases.

**The staging folder is not scratch space.** `generate-manifest.py` treats every loose
file in `release/` as something to ship and writes it into the manifest with a URL on
the version tag. Build archives somewhere else. `release.py` now fails on any file in
`release/` that is not in the manifest or the upload set, but the discipline is what
matters.

**An archive is only as good as what is inside it.** `Worldmaps.zip` was built from
`*.png` and shipped without `map_world_bounds.csv` - the only copy of it anywhere in a
release. Every map rendered as a blank grid floor. `ARCHIVE_MUST_CONTAIN` in
`generate-assets-hashes.py` and `generate-manifest.py` now guards that file, and
`required_files` in `package-release.py` does the same for `shaders.zip` and
`data.zip`; extend them when an archive gains a single-source dependency.

Publishing checklist:

- [ ] `release.py` finishes with "is ready in" and exits 0. It has checked the
      version against `Core/Version.h`, every manifest entry against the staged
      files, the signature against `signing_public_key`, and (unless `--offline`)
      that the assets tag carries every static asset
- [ ] Download the published `Worldmaps.zip` and confirm the bounds csv is in it
- [ ] Install into an empty folder, run the updater, and open a dungeon. A pull
      that renders is worth more than any hash check

## A worked example: 12.0 to 12.1

What the 12.1 roll-forward actually took, as a sense of scale.

**Layouts.** All 23 tables matched the recorded hashes, so no parser work. That is the
good case and it is common - the 12.0 to 12.1 bump before it moved one table. Convert
first and diff before planning anything.

**The dump matters more than the patch number.** A PTR dump and a retail dump of the
"same" 12.1 differ: retail carried 108 more spells and a changed `dungeonencounter`.
Pull from the client you are shipping against.

**Child maps are easy to miss.** Murder Row rendered the wrong room because uimap 2433
has two child floors, 2434 and 2435, and neither had a PNG. The app asked for a floor
it had no image for and kept whatever texture was already loaded, so the failure looked
like a bad map rather than a missing one. Resolve a season's maps through the
`parentUiMapID` chain, not by instance name.

**Extraction is partial by default.** Of 24 tiles for those two floors, rustydemon got
16; the rest were absent locally and 404 from the CDN. CASC Explorer had all of them.
See [CASC-EXTRACTION.md](CASC-EXTRACTION.md), and check the other tool before
concluding a file was never shipped.

**Count-only reporting hides real problems.** The icon culler reported "20 missing" for
months. Named, 17 were mounts and pets that cannot appear in a combat log and 3 were
current raid tier trinket procs that render as blank squares in a meter. Separately,
`build_icon_pack.py` swapped `.blp` for `.png` before lowercasing, so uppercase table
entries never matched a file: **188 icons had been silently absent from every shipped
pack.** Both tools now name what they could not find.

## What ships

`data.zip` packages all of `AwowLab/data`, so anything left there is downloaded by
every user on every update. Keep debug artifacts out of it.

Loaded at runtime:

| File | Read by |
|---|---|
| `spell_data_{version}.bin` | `Database/SpellDataTable.cpp` |
| `cooldown_modifiers_{version}.json` | `Database/CooldownModifiers/CooldownModifierRegistry.cpp` |
| `hero_talents_{version}.json` | `Core/HeroTalents.cpp` |
| `haste_buffs_{version}.json` | `Database/HasteBuffData.cpp` |
| `season_models_{version}.awmp` | `UIManager/src/Geometry/BakedModelPack.cpp` |
| `charge_spells.json` | `Core/EncounterDataLoader.cpp` |
| `spell_icon_table.bin` | `UIManager/src/UI/SpellIconLoader.cpp` |
| `spellnames/spell_names.csv.gz` | `Core/SpellNameDatabase.cpp` |
| `lang/*.csv`, `fonts/` | `Core/LocalizationManager.cpp`, `UIManager/src/Core/ImGuiManager.cpp` |
| `triggers/` | `AnnotationManager/src/AnnotationManager.cpp` |
| `PNG/Worldmaps/map_world_bounds.csv` | `UIManager/src/UI/WorldMapTextureManager.cpp` |
| `PNG/Worldmaps/{mapId}.png` | `UIManager/src/UI/WorldMapTextureManager.cpp` |
| `PNG/EncounterMaps/encounter_*.png` (assets release only, not in git) | `UIManager/src/UI/EncounterTextureManager.cpp` |

## Notes

- `tools/` is excluded by default in `.gitignore`; the pipeline is opted back in
  with `!` rules. Because git will not descend into an excluded directory, a
  directory must be re-included before any file inside it can be. Generated output
  and the DB2 dumps stay out.
- `tools/translate/config.json` holds the DeepL API key and must never be
  committed. Dry-run `git add -An` before staging
  under `tools/`.
