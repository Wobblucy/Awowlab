#!/usr/bin/env python3
"""
Spell Icon Table Generator

Generates a CSV mapping of WoW spell IDs to their icon filenames.
Used by AwowLab to display spell icons in the UI.

Usage:
  python generate_spell_icon_table.py <db2_folder>
  python generate_spell_icon_table.py      (reads AWOWLAB_DB2_DIR)

Without a folder and without AWOWLAB_DB2_DIR set, it prompts for one
(double-click runs it this way), defaulting to rawdb2/<version>.

Required db2 files (either naming convention):
  - spellmisc.txt or spellmisc_export.txt
  - manifestinterfacedata.txt or manifestinterfacedata_export.txt

Output:
  - output/spell_icon_table.csv
"""

import csv
import os
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from db2_layouts import DB2_DIR_ENV, default_db2_dir, resolve_db2_dir


def find_file(db2_dir: Path, base_name: str) -> Path:
    """
    Find a DB2 export file, checking both naming conventions:
      - {base_name}.txt (from --keep-name export)
      - {base_name}_export.txt (legacy format)
    Returns the path if found, None otherwise.
    """
    candidates = [
        db2_dir / f"{base_name}.txt",
        db2_dir / f"{base_name}_export.txt",
    ]
    for path in candidates:
        if path.exists():
            return path
    return None


def main():
    script_dir = Path(__file__).parent
    output_dir = script_dir / "output"
    interactive_mode = len(sys.argv) <= 1 and not os.environ.get(DB2_DIR_ENV)

    print("=" * 60)
    print("Spell Icon Table Generator")
    print("=" * 60)
    print()

    # Get path to db2 files
    if len(sys.argv) > 1:
        db2_path = resolve_db2_dir(sys.argv[1])
    elif os.environ.get(DB2_DIR_ENV):
        db2_path = resolve_db2_dir()
    else:
        default_path = default_db2_dir()
        if not default_path.exists():
            default_path = script_dir / "raw-data"

        print(f"Default path: {default_path}")
        db2_input = input("Enter path to DB2 export files (or press Enter for default): ").strip().strip('"\'')

        db2_path = Path(db2_input) if db2_input else default_path

        if not db2_path.exists():
            print(f"Error: Path does not exist: {db2_path}")
            print(f"Pass the export folder as an argument, or set {DB2_DIR_ENV} to it.")
            input("Press Enter to exit...")
            sys.exit(1)

    print(f"Using DB2 folder: {db2_path}")

    # Check required files
    required_files = ["spellmisc", "manifestinterfacedata"]
    file_paths = {}

    print()
    print("Checking required files...")

    for base_name in required_files:
        filepath = find_file(db2_path, base_name)
        if filepath is None:
            print(f"  ERROR: Missing required file: {base_name}.txt or {base_name}_export.txt")
            print()
            print("To get these files, export DB2 files using:")
            print('  php export_batch.php <source> <target> --keep-name')
            if interactive_mode:
                input("Press Enter to exit...")
            sys.exit(1)
        file_paths[base_name] = filepath
        print(f"  Found: {filepath.name}")

    # Ensure output directory exists
    output_dir.mkdir(parents=True, exist_ok=True)
    output_file = output_dir / "spell_icon_table.csv"

    print()
    print("=" * 60)
    print(f"Reading {file_paths['manifestinterfacedata'].name}...")

    # First, build a lookup dictionary for manifest ID -> icon filename
    manifest_lookup = {}
    with open(file_paths['manifestinterfacedata'], 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('Layout:'):
                continue

            # Parse format: "5927625: Interface\ICONS\,INV_Ability_FarseerShaman_AncestralSwiftness.blp"
            match = re.match(r'(\d+):\s*(.+),(.+)', line)
            if match:
                manifest_id = match.group(1)
                icon_path = match.group(2)
                icon_filename = match.group(3)
                manifest_lookup[manifest_id] = {
                    'path': icon_path,
                    'filename': icon_filename
                }

    print(f"Loaded {len(manifest_lookup)} manifest entries")

    print(f"Reading {file_paths['spellmisc'].name}...")

    # Now parse spellmisc.txt
    results = []
    with open(file_paths['spellmisc'], 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('Layout:'):
                continue

            # Parse the line - format: "75: [array],val1,val2,...,icon_manifest_id,...,spell_id"
            # We need to split by comma, but skip the array part
            if ':' not in line:
                continue

            # Split at the colon to separate the line number from the data
            parts = line.split(':', 1)
            if len(parts) != 2:
                continue

            data = parts[1].strip()

            # Find the end of the array (marked by ']')
            array_end = data.find(']')
            if array_end == -1:
                continue

            # Get everything after the array
            after_array = data[array_end + 1:].strip()

            # Remove leading comma if present
            if after_array.startswith(','):
                after_array = after_array[1:]

            # Split the remaining values by comma
            values = after_array.split(',')

            if len(values) < 7:
                continue

            # Get spell ID (last value) and icon manifest ID (7th from last)
            spell_id = values[-1].strip()
            icon_manifest_id = values[-7].strip()

            # Look up the icon filename
            icon_info = manifest_lookup.get(icon_manifest_id, {})
            icon_filename = icon_info.get('filename', 'NOT_FOUND')
            icon_path = icon_info.get('path', '')

            results.append({
                'spell_id': spell_id,
                'icon_manifest_id': icon_manifest_id,
                'icon_path': icon_path,
                'icon_filename': icon_filename
            })

    print(f"Processed {len(results)} spell entries")

    # Write to CSV
    print(f"Writing results to {output_file}...")
    with open(output_file, 'w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=['spell_id', 'icon_manifest_id', 'icon_path', 'icon_filename'])
        writer.writeheader()
        writer.writerows(results)

    print()
    print("=" * 60)
    print(f"SUCCESS! Generated table with {len(results)} entries")
    print(f"Output file: {output_file}")
    print("=" * 60)

    # Show sample entries
    print()
    print("Sample entries:")
    for i, row in enumerate(results[:5]):
        print(f"  Spell {row['spell_id']}: {row['icon_filename']}")

    if interactive_mode:
        print()
        input("Press Enter to exit...")


if __name__ == '__main__':
    main()
