"""
Icon Pack Culler - Single script to reduce icon pack size.

Combines spell IDs from:
- DB2 boss abilities (JournalEncounterSection)
- WCL cache (all combat abilities from loaded logs)
- Class abilities (spell_data binary)
- Recent spells (high ID threshold)

Then copies only required icons to output directory.
"""

import argparse
import csv
import json
import os
import re
import shutil
import struct
import subprocess
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from db2_layouts import find_export


# =============================================================================
# CONFIGURATION
# =============================================================================
# Shared settings live in config.py so the season's instance list, the version
# and the export directory are stated once. Only this script's own output layout
# is defined here.

import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from config import (
    TARGET_INSTANCES,
    RECENT_SPELL_THRESHOLD,
    VERSION,
    ROOT_DIR,
    SCRIPT_DIR,
    DB2_DIR as DB2_DIR_STR,
    WCL_CACHE_DIR as WCL_CACHE_DIR_STR,
    SPELL_DATA_BIN as SPELL_DATA_BIN_STR,
    ICON_TABLE_CSV as ICON_TABLE_CSV_STR,
)

DB2_DIR = Path(DB2_DIR_STR)
WCL_CACHE_DIR = Path(WCL_CACHE_DIR_STR)
SPELL_DATA_BIN = Path(SPELL_DATA_BIN_STR)
ICON_TABLE_CSV = Path(ICON_TABLE_CSV_STR)

# This script culls the already-compressed pack, not the raw icons.
SOURCE_ICONS_DIR = ROOT_DIR / 'icon-pack' / 'PNG' / 'Spell Icons - Compressed'
PNGQUANT_EXE = ROOT_DIR / 'tools' / 'worldmap-builder' / 'pngquant.exe'

# Output paths
OUTPUT_DIR = SCRIPT_DIR / 'output'
CULLED_DIR = OUTPUT_DIR / 'icon-pack-culled'
COMPRESSED_DIR = OUTPUT_DIR / 'icon-pack-compressed'

# Compression quality (0-100)
COMPRESSION_QUALITY = 50


# =============================================================================
# EXTRACTION FUNCTIONS
# =============================================================================

def extract_boss_spells_from_db2():
    """Extract boss ability spell IDs from DB2 JournalEncounterSection."""
    print("\n[1/4] Extracting boss spell IDs from DB2...")

    if not DB2_DIR.exists():
        print(f"  Warning: DB2 directory not found: {DB2_DIR}")
        return set()

    # Step 1: Get encounter IDs for target instances
    encounter_file = find_export(DB2_DIR, 'journalencounter')
    encounter_ids = set()

    if encounter_file is not None:
        with open(encounter_file, 'r', encoding='utf-8') as f:
            for line in f:
                if line.startswith('Layout:'):
                    continue

                colon_idx = line.find(':')
                if colon_idx == -1:
                    continue

                try:
                    encounter_id = int(line[:colon_idx].strip())
                except ValueError:
                    continue

                bracket_start = line.find('[')
                bracket_end = line.find(']', bracket_start)

                if bracket_start == -1 or bracket_end == -1:
                    continue

                after_coords = line[bracket_end + 1:].strip()
                if after_coords.startswith(','):
                    after_coords = after_coords[1:]

                remaining_parts = after_coords.split(',')
                if len(remaining_parts) >= 2:
                    instance_id_str = remaining_parts[1].strip()
                    if instance_id_str.isdigit():
                        instance_id = int(instance_id_str)
                        if instance_id in TARGET_INSTANCES:
                            encounter_ids.add(encounter_id)

    print(f"  Found {len(encounter_ids)} encounters in target instances")

    # Step 2: Extract spell IDs from sections
    section_file = find_export(DB2_DIR, 'journalencountersection')
    spell_ids = set()

    dollar_pattern = re.compile(r'\$(\d+)[sAdtoi\d]')
    hyperlink_pattern = re.compile(r'\|Hspell:(\d+)\|h')

    if section_file is not None:
        with open(section_file, 'r', encoding='utf-8') as f:
            for line in f:
                if line.startswith('Layout:'):
                    continue

                parts = line.split(',')

                # Check if any part matches our encounter IDs
                line_matches_encounter = False
                for part in parts:
                    part = part.strip()
                    if part.isdigit():
                        if int(part) in encounter_ids:
                            line_matches_encounter = True
                            break

                if line_matches_encounter:
                    # Extract spell IDs from hyperlinks
                    for match in dollar_pattern.findall(line):
                        spell_ids.add(int(match))
                    for match in hyperlink_pattern.findall(line):
                        spell_ids.add(int(match))

                    # Also check position 10 for spell ID (section header format)
                    if len(parts) >= 11:
                        spell_id_str = parts[10].strip()
                        if spell_id_str.isdigit():
                            spell_id = int(spell_id_str)
                            if spell_id > 1000:
                                spell_ids.add(spell_id)

    print(f"  Found {len(spell_ids)} unique boss spell IDs")
    return spell_ids


def extract_cache_spells():
    """Extract all abilityGameID from cached WCL events."""
    print("\n[2/4] Extracting spell IDs from WCL cache...")

    spell_ids = set()

    if not WCL_CACHE_DIR.exists():
        print(f"  Warning: Cache directory not found: {WCL_CACHE_DIR}")
        return spell_ids

    page_files = list(WCL_CACHE_DIR.glob('*/fight*/page*.json'))
    print(f"  Found {len(page_files)} cache page files")

    for page_file in page_files:
        try:
            with open(page_file, 'r', encoding='utf-8') as f:
                events = json.load(f)

                if isinstance(events, list):
                    for event in events:
                        if isinstance(event, dict) and 'abilityGameID' in event:
                            ability_id = event['abilityGameID']
                            if isinstance(ability_id, int) and ability_id > 0:
                                spell_ids.add(ability_id)
        except (json.JSONDecodeError, IOError):
            continue

    print(f"  Found {len(spell_ids)} unique spell IDs from cache")
    return spell_ids


def read_class_spells():
    """Read spell IDs from spell_data binary file."""
    print("\n[3/4] Reading class spell IDs from spell_data.bin...")

    spell_ids = set()

    if not SPELL_DATA_BIN.exists():
        print(f"  Warning: Spell data file not found: {SPELL_DATA_BIN}")
        return spell_ids

    with open(SPELL_DATA_BIN, 'rb') as f:
        header = f.read(4)
        if len(header) < 4:
            print("  Warning: Invalid spell data file")
            return spell_ids

        count = struct.unpack('<I', header)[0]
        print(f"  Header indicates {count} spell records")

        for i in range(count):
            record = f.read(20)
            if len(record) < 20:
                break

            spell_id = struct.unpack('<I', record[:4])[0]
            if spell_id > 0:
                spell_ids.add(spell_id)

    print(f"  Found {len(spell_ids)} unique class spell IDs")
    return spell_ids


def get_recent_spells(icon_mapping):
    """Get spell IDs above threshold (recent additions)."""
    print("\n[4/4] Finding recent spells...")

    recent = {sid for sid in icon_mapping.keys() if sid >= RECENT_SPELL_THRESHOLD}
    print(f"  Found {len(recent)} recent spells (ID >= {RECENT_SPELL_THRESHOLD})")
    return recent


# =============================================================================
# ICON PROCESSING FUNCTIONS
# =============================================================================

def load_required_icons():
    """Load icon filenames from required_icons.txt.

    These are icons that must always be included regardless of spell ID,
    such as specialization icons used by SpecIconLoader.
    """
    required_file = SCRIPT_DIR / 'required_icons.txt'
    icons = set()

    if not required_file.exists():
        print(f"  Warning: required_icons.txt not found: {required_file}")
        return icons

    with open(required_file, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            # Skip empty lines and comments
            if line and not line.startswith('#'):
                icons.add(line.lower())

    return icons


def load_icon_mapping():
    """Load spell_icon_table.csv -> {spell_id: icon_filename}."""
    mapping = {}

    if not ICON_TABLE_CSV.exists():
        print(f"  Warning: Icon table not found: {ICON_TABLE_CSV}")
        return mapping

    with open(ICON_TABLE_CSV, 'r', encoding='utf-8') as f:
        reader = csv.reader(f)
        next(reader, None)  # Skip header

        for row in reader:
            if len(row) >= 4:
                try:
                    spell_id = int(row[0])
                    icon_filename = row[3].strip()

                    if icon_filename and 'NOT_FOUND' not in icon_filename.upper():
                        icon_filename = icon_filename.replace('.blp', '.png').lower()
                        mapping[spell_id] = icon_filename
                except (ValueError, IndexError):
                    continue

    return mapping


def cull_icons(spell_ids, icon_mapping):
    """Copy only required icons to output directory."""
    print("\nCulling icons...")

    CULLED_DIR.mkdir(parents=True, exist_ok=True)

    if not SOURCE_ICONS_DIR.exists():
        print(f"  Warning: Source icons directory not found: {SOURCE_ICONS_DIR}")
        return 0, 0, 0

    # Get unique icon filenames needed
    required_icons = set()
    for spell_id in spell_ids:
        if spell_id in icon_mapping:
            required_icons.add(icon_mapping[spell_id])

    print(f"  {len(required_icons)} unique icon files from spell IDs")

    # Also include icons from required_icons.txt (spec icons, etc.)
    manual_icons = load_required_icons()
    required_icons.update(manual_icons)
    print(f"  Added {len(manual_icons)} icons from required_icons.txt")
    print(f"  Total: {len(required_icons)} icons to copy")

    # Clear output directory
    if CULLED_DIR.exists():
        for f in CULLED_DIR.glob('*'):
            f.unlink()

    # Copy icons
    copied = 0
    missing = 0

    for icon_name in required_icons:
        src = SOURCE_ICONS_DIR / icon_name
        if src.exists():
            dst = CULLED_DIR / icon_name
            shutil.copy2(src, dst)
            copied += 1
        else:
            missing += 1

    # Copy spell_icon_table.csv
    if ICON_TABLE_CSV.exists():
        shutil.copy2(ICON_TABLE_CSV, CULLED_DIR / 'spell_icon_table.csv')

    return copied, missing, len(required_icons)


def compress_icons():
    """Compress culled icons with pngquant."""
    print("\nCompressing icons with pngquant...")

    if not PNGQUANT_EXE.exists():
        print(f"  Warning: pngquant not found: {PNGQUANT_EXE}")
        print("  Skipping compression step")
        return False

    COMPRESSED_DIR.mkdir(parents=True, exist_ok=True)

    # Clear output directory
    for f in COMPRESSED_DIR.glob('*'):
        f.unlink()

    png_files = list(CULLED_DIR.glob('*.png'))
    total = len(png_files)
    print(f"  Compressing {total} icons...")

    success = 0
    for i, png_file in enumerate(png_files, 1):
        output_file = COMPRESSED_DIR / png_file.name
        cmd = [
            str(PNGQUANT_EXE),
            f'--quality=0-{COMPRESSION_QUALITY}',
            '--force',
            '--output', str(output_file),
            str(png_file)
        ]

        try:
            subprocess.run(cmd, check=True, capture_output=True)
            success += 1
        except subprocess.CalledProcessError:
            # If compression fails, copy original
            shutil.copy2(png_file, output_file)
            success += 1

        if i % 2000 == 0:
            print(f"    {i}/{total}...")

    # Copy spell_icon_table.csv
    csv_src = CULLED_DIR / 'spell_icon_table.csv'
    if csv_src.exists():
        shutil.copy2(csv_src, COMPRESSED_DIR / 'spell_icon_table.csv')

    # Calculate sizes
    original = sum(f.stat().st_size for f in CULLED_DIR.glob('*.png'))
    compressed = sum(f.stat().st_size for f in COMPRESSED_DIR.glob('*.png'))

    print(f"  Compressed {success}/{total} icons")
    print(f"  Original: {original/1024/1024:.1f} MB")
    print(f"  Compressed: {compressed/1024/1024:.1f} MB")
    print(f"  Reduction: {(1 - compressed/original)*100:.0f}%")

    return True


def generate_report(boss_ids, cache_ids, class_ids, recent_ids, all_ids, copied, missing, total):
    """Generate a summary report."""
    report_file = OUTPUT_DIR / 'cull_report.txt'

    with open(report_file, 'w') as f:
        f.write("Icon Pack Culling Report\n")
        f.write("========================\n\n")
        f.write("Spell ID Sources:\n")
        f.write(f"  Boss abilities (DB2):     {len(boss_ids):>8}\n")
        f.write(f"  Cache abilities (WCL):    {len(cache_ids):>8}\n")
        f.write(f"  Class abilities (binary): {len(class_ids):>8}\n")
        f.write(f"  Recent spells (high ID):  {len(recent_ids):>8}\n")
        f.write(f"  -----------------------------------\n")
        f.write(f"  Total unique spell IDs:   {len(all_ids):>8}\n\n")
        f.write("Icon Culling Results:\n")
        f.write(f"  Icons required:           {total:>8}\n")
        f.write(f"  Icons copied:             {copied:>8}\n")
        f.write(f"  Icons missing:            {missing:>8}\n")

    print(f"\nReport saved to {report_file}")


# =============================================================================
# MAIN
# =============================================================================

def parse_args():
    """Point the paths above somewhere else; each defaults to the value above."""
    global SOURCE_ICONS_DIR, OUTPUT_DIR, CULLED_DIR, COMPRESSED_DIR
    global DB2_DIR, ICON_TABLE_CSV, SPELL_DATA_BIN, PNGQUANT_EXE

    parser = argparse.ArgumentParser(description="Keep only the icons the app can show.")
    parser.add_argument("--source", default=str(SOURCE_ICONS_DIR),
                        help="compressed icon set to cull (default: icon-pack/PNG/Spell Icons - Compressed)")
    parser.add_argument("--out", default=str(OUTPUT_DIR),
                        help="folder for the culled sets and report (default: tools/icon-culler/output)")
    parser.add_argument("--db2-dir", default=str(DB2_DIR),
                        help="DB2 text exports (default: AWOWLAB_DB2_DIR, else rawdb2/<version>)")
    parser.add_argument("--icon-table", default=str(ICON_TABLE_CSV),
                        help="spell_icon_table.csv from generate_spell_icon_table.py")
    parser.add_argument("--spell-data", default=str(SPELL_DATA_BIN),
                        help=f"spell data binary (default: AwowLab/data/spell_data_{VERSION}.bin)")
    parser.add_argument("--pngquant", default=str(PNGQUANT_EXE),
                        help="pngquant executable (default: tools/worldmap-builder/pngquant.exe)")
    args = parser.parse_args()

    SOURCE_ICONS_DIR = Path(args.source)
    OUTPUT_DIR = Path(args.out)
    CULLED_DIR = OUTPUT_DIR / 'icon-pack-culled'
    COMPRESSED_DIR = OUTPUT_DIR / 'icon-pack-compressed'
    DB2_DIR = Path(args.db2_dir)
    ICON_TABLE_CSV = Path(args.icon_table)
    SPELL_DATA_BIN = Path(args.spell_data)
    PNGQUANT_EXE = Path(args.pngquant)


def main():
    parse_args()

    print("=" * 60)
    print("Icon Pack Culler")
    print("=" * 60)
    print(f"  Source: {SOURCE_ICONS_DIR}")

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    # Load icon mapping first (needed for recent spells)
    print("\nLoading icon mapping...")
    icon_mapping = load_icon_mapping()
    print(f"  Loaded {len(icon_mapping)} spell-to-icon mappings")

    # Extract spell IDs from all sources
    boss_ids = extract_boss_spells_from_db2()
    cache_ids = extract_cache_spells()
    class_ids = read_class_spells()
    recent_ids = get_recent_spells(icon_mapping)

    # Combine all sources
    all_spell_ids = boss_ids | cache_ids | class_ids | recent_ids

    print("\n" + "=" * 60)
    print("Summary")
    print("=" * 60)
    print(f"  Boss abilities:     {len(boss_ids):>8}")
    print(f"  Cache abilities:    {len(cache_ids):>8}")
    print(f"  Class abilities:    {len(class_ids):>8}")
    print(f"  Recent spells:      {len(recent_ids):>8}")
    print(f"  Total unique IDs:   {len(all_spell_ids):>8}")

    # Cull icons
    copied, missing, total = cull_icons(all_spell_ids, icon_mapping)

    print("\n" + "=" * 60)
    print("Results")
    print("=" * 60)
    print(f"  Icons copied:  {copied}")
    print(f"  Icons missing: {missing}")
    print(f"  Total needed:  {total}")

    # Compress icons
    compress_icons()

    # Generate report
    generate_report(boss_ids, cache_ids, class_ids, recent_ids, all_spell_ids, copied, missing, total)

    # Final size
    if COMPRESSED_DIR.exists():
        total_size = sum(f.stat().st_size for f in COMPRESSED_DIR.glob('*') if f.is_file())
        print(f"\nFinal compressed pack: {total_size / (1024*1024):.1f} MB")
        print(f"Output: {COMPRESSED_DIR}")
    elif CULLED_DIR.exists():
        total_size = sum(f.stat().st_size for f in CULLED_DIR.glob('*') if f.is_file())
        print(f"\nCulled pack size: {total_size / (1024*1024):.1f} MB")
        print(f"Output: {CULLED_DIR}")

    print("\nDone!")


if __name__ == '__main__':
    main()
