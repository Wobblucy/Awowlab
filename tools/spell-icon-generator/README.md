# Spell Icon Table Generator

Generates a CSV mapping of WoW spell IDs to their icon filenames. This file is used by the AwowLab application to display spell icons in the UI.

## Requirements

- Python 3.8+
- Raw DB2 exports (place in `raw-data/` folder):
  - `spellmisc_export.txt` - From SpellMisc.db2
  - `manifestinterfacedata_export.txt` - From ManifestInterfaceData.db2

## How to Get DB2 Export Files

1. Visit https://wow.tools/dbc/
2. Search for "SpellMisc" and "ManifestInterfaceData"
3. Export each table as text format
4. Place in `raw-data/` folder

## Usage

1. Place DB2 export files in `raw-data/`
2. Run: `python generate_spell_icon_table.py`
3. Follow the prompts (press Enter for defaults)
4. Output will be saved to `output/spell_icon_table.csv`

## Output

**spell_icon_table.csv** - CSV with columns:
- `spell_id` - WoW spell ID
- `icon_manifest_id` - Internal manifest reference
- `icon_path` - Folder path (e.g., `Interface\ICONS\`)
- `icon_filename` - Icon filename (e.g., `Spell_Fire_Fireball.blp`)

## Integration with AwowLab

The generated CSV is automatically copied to the build directory by CMake. After regenerating:
1. Rebuild AwowLab to copy the updated CSV
2. Or manually copy to your executable directory

## Note for Releases

Before creating a release build, ensure `output/spell_icon_table.csv` exists and is up to date.
