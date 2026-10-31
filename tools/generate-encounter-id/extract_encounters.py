#!/usr/bin/env python3
"""
Extract encounter ID -> name mapping from WoW db2 dungeonencounter export.

Usage:
    python extract_encounters.py [input_file] [output_file]

Without an input file it reads dungeonencounter from AWOWLAB_DB2_DIR, else
rawdb2/<version>.

Example:
    python extract_encounters.py "<exports>/dungeonencounter.txt" output/encounter_names.csv

Input format (dungeonencounter_export.txt):
    Layout: 35863dc4
    3135: Dimensius, the All-Devouring,3135,2810,0,7000,27881,7,6,0,4294967295,4,2810

Output format (CSV):
    encounter_id,name
    3135,Dimensius, the All-Devouring
"""

import sys
import os
import re
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from db2_layouts import find_export, resolve_db2_dir


def parse_dungeonencounter(input_path: str) -> list[tuple[int, str]]:
    """
    Parse dungeonencounter_export.txt and extract encounter ID -> name mappings.

    Returns list of (encounter_id, name) tuples.
    """
    encounters = []

    with open(input_path, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()

            # Skip header line (Layout: ...)
            if line.startswith('Layout:') or not line:
                continue

            # Format: "ID: Name,ID,MapID,..."
            # Example: "3135: Dimensius, the All-Devouring,3135,2810,..."
            match = re.match(r'^(\d+):\s*(.+)$', line)
            if not match:
                continue

            encounter_id = int(match.group(1))
            rest = match.group(2)

            # The name ends at the first comma followed by a number (the encounter ID repeated)
            # We need to handle names with commas like "Theralion and Valiona"
            # Find the pattern: ,<encounter_id>,
            name_end_pattern = f',{encounter_id},'
            name_end_idx = rest.find(name_end_pattern)

            if name_end_idx != -1:
                name = rest[:name_end_idx].strip()
            else:
                # Fallback: take everything before the first comma
                name = rest.split(',')[0].strip()

            encounters.append((encounter_id, name))

    return encounters


def write_csv(encounters: list[tuple[int, str]], output_path: str):
    """Write encounters to CSV file."""
    os.makedirs(os.path.dirname(output_path) or '.', exist_ok=True)

    with open(output_path, 'w', encoding='utf-8', newline='') as f:
        f.write('encounter_id,name\n')
        for encounter_id, name in sorted(encounters, key=lambda x: x[0]):
            # Escape names containing commas by quoting
            if ',' in name or '"' in name:
                name = '"' + name.replace('"', '""') + '"'
            f.write(f'{encounter_id},{name}\n')


def write_binary(encounters: list[tuple[int, str]], output_path: str):
    """
    Write encounters to compact binary format for fast loading.

    Format:
        4 bytes: uint32 count
        For each entry:
            4 bytes: uint32 encounter_id
            2 bytes: uint16 name_length
            N bytes: UTF-8 name (no null terminator)
    """
    import struct

    os.makedirs(os.path.dirname(output_path) or '.', exist_ok=True)

    sorted_encounters = sorted(encounters, key=lambda x: x[0])

    with open(output_path, 'wb') as f:
        # Write count
        f.write(struct.pack('<I', len(sorted_encounters)))

        for encounter_id, name in sorted_encounters:
            name_bytes = name.encode('utf-8')
            f.write(struct.pack('<I', encounter_id))
            f.write(struct.pack('<H', len(name_bytes)))
            f.write(name_bytes)


def main():
    if len(sys.argv) > 1:
        input_path = sys.argv[1]
    else:
        db2_dir = resolve_db2_dir()
        input_path = find_export(db2_dir, 'dungeonencounter')
        if input_path is None:
            print(f"Error: no dungeonencounter export in {db2_dir}")
            sys.exit(1)
        input_path = str(input_path)

    # Default output paths
    script_dir = Path(__file__).parent
    output_dir = script_dir / 'output'

    csv_output = sys.argv[2] if len(sys.argv) > 2 else str(output_dir / 'encounter_names.csv')
    bin_output = str(output_dir / 'encounter_names.bin')

    print(f"Reading: {input_path}")
    encounters = parse_dungeonencounter(input_path)
    print(f"Found {len(encounters)} encounters")

    # Write CSV
    write_csv(encounters, csv_output)
    print(f"Wrote CSV: {csv_output}")

    # Write binary
    write_binary(encounters, bin_output)
    print(f"Wrote binary: {bin_output}")

    # Print some examples
    print("\nSample encounters:")
    for eid, name in sorted(encounters, key=lambda x: -x[0])[:10]:
        print(f"  {eid}: {name}")


if __name__ == '__main__':
    main()
