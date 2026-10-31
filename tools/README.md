# Tools

Scripts that set up a build, regenerate the game data the app ships, and
package releases. Building and running AwowLab from source needs only the first
section; everything else is for refreshing data after a game patch or for
publishing.

Run `python tools/check_setup.py` first. It reports, step by step, which
programs, packages and settings are present and which are missing.

## Setting up

| Script | Does |
|--------|------|
| `check_setup.py` | Reports what each pipeline step needs and what is missing. Standard library only. |
| `requirements.txt` | Python packages the tools import: `pip install -r tools/requirements.txt` |
| `fetch-assets.py` | Downloads the spell icons and maps a source build does not include, from the assets release, and checks each against `release/assets-hashes.json`. Optional: the data steps below can produce the same files from the game. |

Two settings are read by every data script:

| Setting | Meaning | Default |
|---------|---------|---------|
| `AWOWLAB_DB2_DIR` | Folder of DB2 text exports | `rawdb2/<version>` at the repository root |
| `AWOWLAB_WOW_DIR` | World of Warcraft install folder | `C:/Program Files (x86)/World of Warcraft` |

## Game data, in pipeline order

The full procedure for a new game patch, with commands, is
`docs/UPDATE-FLOW.md`. The folders it uses:

| Stage | Folder | Produces |
|-------|--------|----------|
| 1. Export game tables | `db2-export/` | DB2 text exports, straight from the game install |
| | `db2_layouts.py`, `db2_layouts.json` | Checks every export's column layout against the version the scripts were written for |
| 2. Spell data | `spell-data-generator/`, `db2_extractor/` | `AwowLab/data/spell_data_<version>.bin`, cooldown modifiers, hero talents |
| 2b. Haste buffs | `db2_extractor/build_haste_buffs.py` | `AwowLab/data/haste_buffs_<version>.json` |
| 3. Spell names | `spell-name-converter/` | `AwowLab/data/spellnames/spell_names.csv.gz` |
| 4. Spell icons | `spell-icon-generator/`, `icon-export/`, `icon-culler/` | `AwowLab/data/spell_icon_table.bin`, and the icon images straight from the game install |
| 5. World maps | `worldmap-builder/` | `AwowLab/PNG/Worldmaps/` images and `map_world_bounds.csv` |
| 6. Encounters | `generate-encounter-id/` | Encounter id and name table |
| 7. Arena maps | `arena-maps/`, `encounter-map-compressor/` | `AwowLab/PNG/EncounterMaps/` |
| 8. Enemy models | `model-extractor/` | `AwowLab/data/season_models_<version>.awmp` |

Several stages run outside tools borrowed from other projects (wow.export,
db2reader). Our changes to them are kept as patches in `external/`, with notes
on rebuilding each.

## Translations

`translate/` copies new interface strings into every language file and sends
them for machine translation. The language files in `AwowLab/data/lang/` are
the source; translators change them by pull request. Machine translation needs
a DeepL key of your own; see `translate/README.md`.

## Releases

| Folder | Does |
|--------|------|
| `release/` | Packages a build, writes and signs the update manifest, records the static asset checksums. Signing is described in `release/SIGNING.md`. |

## Developer tools

`analysis/` holds tools for working on the app itself: an action priority list
checker for analysis packages, and a replayer that feeds a saved combat log
into a file the app tails as if it were live.
