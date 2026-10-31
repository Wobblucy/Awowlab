"""Read class ability spell IDs from spell_data binary file."""

import struct
from pathlib import Path
from config import SPELL_DATA_BIN, OUTPUT_DIR


def read_spell_data(filepath):
    """Read spell IDs from spell_data binary file.

    Format:
    - 4-byte header: record count (uint32_t)
    - 20-byte records: spell_id (u32), gcd_ms (i32), cd_ms (i32), cast_ms (i32), flags (4 bytes)
    """
    spell_ids = set()
    filepath = Path(filepath)

    if not filepath.exists():
        print(f"  Warning: Spell data file not found: {filepath}")
        return spell_ids

    with open(filepath, 'rb') as f:
        # Read header
        header = f.read(4)
        if len(header) < 4:
            print("  Warning: Invalid spell data file (too short)")
            return spell_ids

        count = struct.unpack('<I', header)[0]
        print(f"  Header indicates {count} spell records")

        # Read records
        for i in range(count):
            record = f.read(20)
            if len(record) < 20:
                print(f"  Warning: Unexpected end of file at record {i}")
                break

            spell_id = struct.unpack('<I', record[:4])[0]
            if spell_id > 0:
                spell_ids.add(spell_id)

    return spell_ids


def main():
    output_dir = Path(OUTPUT_DIR)
    output_dir.mkdir(parents=True, exist_ok=True)

    print("Reading class spell IDs from spell_data.bin...")

    spell_ids = read_spell_data(SPELL_DATA_BIN)
    print(f"  Found {len(spell_ids)} unique class spell IDs")

    # Save results
    output_file = output_dir / 'class_spell_ids.txt'
    with open(output_file, 'w') as f:
        for sid in sorted(spell_ids):
            f.write(f"{sid}\n")

    print(f"  Saved to {output_file}")
    return spell_ids


if __name__ == '__main__':
    main()
