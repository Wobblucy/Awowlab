#!/usr/bin/env python3
"""
Generate spell_icon_table.bin from spell_icon_table.csv.

Binary format:
  [Header] (16 bytes)
    uint32_t magic = 0x49434F4E ("ICON")
    uint32_t version = 1
    uint32_t num_filenames (unique icon count)
    uint32_t num_mappings (spell count)

  [String Table] (variable size)
    For each filename:
      uint16_t length
      char[length] filename (no null terminator)

  [Mapping Table] (6 bytes per entry)
    For each mapping (sorted by spell_id):
      uint32_t spell_id
      uint16_t filename_index

Usage:
    python generate_spell_icon_binary.py
    python generate_spell_icon_binary.py input.csv output.bin
"""

import csv
import struct
import sys
from pathlib import Path


MAGIC = 0x49434F4E  # "ICON" in ASCII
VERSION = 1


def generate_binary(csv_path: Path, bin_path: Path) -> None:
    """Convert spell_icon_table.csv to binary format."""

    # Read CSV and collect mappings
    mappings = []  # List of (spell_id, filename)

    print(f"Reading {csv_path}...")
    with open(csv_path, 'r', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        for row in reader:
            spell_id = int(row['spell_id'])
            filename = row['icon_filename']

            # Skip invalid entries
            if not filename or filename == 'NOT_FOUND':
                continue

            # Convert .blp to .png and lowercase (matches runtime behavior)
            if filename.lower().endswith('.blp'):
                filename = filename[:-4] + '.png'
            filename = filename.lower()

            mappings.append((spell_id, filename))

    print(f"  Loaded {len(mappings)} mappings")

    # Build unique filename string table
    unique_filenames = sorted(set(fn for _, fn in mappings))
    filename_to_index = {fn: i for i, fn in enumerate(unique_filenames)}

    print(f"  {len(unique_filenames)} unique filenames")

    # Sort mappings by spell_id for binary search
    mappings.sort(key=lambda x: x[0])

    # Write binary file
    print(f"Writing {bin_path}...")
    with open(bin_path, 'wb') as f:
        # Header (16 bytes)
        f.write(struct.pack('<I', MAGIC))           # magic
        f.write(struct.pack('<I', VERSION))         # version
        f.write(struct.pack('<I', len(unique_filenames)))  # num_filenames
        f.write(struct.pack('<I', len(mappings)))   # num_mappings

        # String table
        for filename in unique_filenames:
            encoded = filename.encode('utf-8')
            f.write(struct.pack('<H', len(encoded)))  # length (2 bytes)
            f.write(encoded)                           # filename bytes

        # Mapping table (6 bytes per entry)
        for spell_id, filename in mappings:
            filename_index = filename_to_index[filename]
            f.write(struct.pack('<I', spell_id))      # spell_id (4 bytes)
            f.write(struct.pack('<H', filename_index)) # filename_index (2 bytes)

    # Report sizes
    csv_size = csv_path.stat().st_size
    bin_size = bin_path.stat().st_size
    reduction = (1 - bin_size / csv_size) * 100

    print(f"\nResults:")
    print(f"  CSV size:    {csv_size / 1024 / 1024:.2f} MB")
    print(f"  Binary size: {bin_size / 1024 / 1024:.2f} MB")
    print(f"  Reduction:   {reduction:.1f}%")
    print(f"\nOutput: {bin_path}")


def main():
    script_dir = Path(__file__).parent

    if len(sys.argv) >= 3:
        csv_path = Path(sys.argv[1])
        bin_path = Path(sys.argv[2])
    else:
        # Default paths
        csv_path = script_dir / "output" / "spell_icon_table.csv"
        bin_path = script_dir.parent.parent / "AwowLab" / "data" / "spell_icon_table.bin"

    if not csv_path.exists():
        print(f"Error: CSV file not found: {csv_path}")
        sys.exit(1)

    # Ensure output directory exists
    bin_path.parent.mkdir(parents=True, exist_ok=True)

    generate_binary(csv_path, bin_path)


if __name__ == "__main__":
    main()
