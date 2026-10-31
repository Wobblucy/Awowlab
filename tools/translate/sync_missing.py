#!/usr/bin/env python3
"""
AwowLab Localization - Sync Missing Strings

Finds strings in en_US.csv that are missing from other locale files
and adds them with the English value as a placeholder.

This allows you to:
1. Add new strings to en_US.csv during development
2. Run this script to propagate to all locales (with English fallback)
3. Later run translate_all.py to get proper translations

Usage:
    python sync_missing.py           # Sync all locales
    python sync_missing.py de_DE     # Sync specific locale
    python sync_missing.py --dry-run # Show what would be added
"""

import os
import sys
from pathlib import Path


# All supported locales (except en_US which is the source)
LOCALES = [
    "de_DE", "fr_FR", "es_MX", "pt_BR",
    "ru_RU", "ko_KR", "zh_CN", "zh_TW"
]


def parse_csv(filepath):
    """
    Parse locale CSV file.
    Returns dict of key -> value, and list of (line_type, content) for structure.
    """
    keys = {}
    structure = []

    with open(filepath, "r", encoding="utf-8-sig") as f:
        for line in f:
            line = line.rstrip("\r\n")

            if not line:
                structure.append(("empty", ""))
            elif line.startswith("#"):
                structure.append(("comment", line))
            elif line == "key,value":
                structure.append(("header", line))
            elif "," in line:
                comma_idx = line.index(",")
                key = line[:comma_idx]
                value = line[comma_idx + 1:]
                # Remove quotes if present
                if value.startswith('"') and value.endswith('"'):
                    value = value[1:-1].replace('""', '"')
                keys[key] = value
                structure.append(("entry", (key, value)))
            else:
                structure.append(("other", line))

    return keys, structure


def find_missing_keys(source_keys, target_keys):
    """Find keys in source that are missing from target."""
    return [k for k in source_keys if k not in target_keys]


def add_missing_to_file(filepath, source_keys, source_structure, missing_keys, dry_run=False):
    """
    Add missing keys to a locale file, preserving structure.
    Inserts each missing key right after its position in the source file.
    """
    if not missing_keys:
        return 0

    # Read current file
    target_keys, target_structure = parse_csv(filepath)

    # Build a map of source key positions
    source_key_positions = {}
    for i, (line_type, content) in enumerate(source_structure):
        if line_type == "entry":
            source_key_positions[content[0]] = i

    # Group missing keys by section (find preceding comment)
    keys_by_section = {}
    for key in missing_keys:
        pos = source_key_positions.get(key, 0)
        # Find the section header (comment with ===) before this key
        section = "# Other"
        for i in range(pos, -1, -1):
            if source_structure[i][0] == "comment":
                comment = source_structure[i][1]
                if "=====" in comment:
                    section = comment
                    break
        if section not in keys_by_section:
            keys_by_section[section] = []
        keys_by_section[section].append((key, source_keys[key]))

    if dry_run:
        print(f"\n  Would add {len(missing_keys)} strings:")
        for section, entries in keys_by_section.items():
            print(f"    {section[:50]}...")
            for key, value in entries:
                print(f"      {key}")
        return len(missing_keys)

    # Append missing keys at end of file, grouped by section
    with open(filepath, "a", encoding="utf-8") as f:
        f.write("\n")
        f.write("# =============================================================================\n")
        f.write("# NEW - Needs Translation (synced from en_US)\n")
        f.write("# =============================================================================\n")

        for key in missing_keys:
            value = source_keys[key]
            # Quote value if needed
            if "," in value or '"' in value:
                value = '"' + value.replace('"', '""') + '"'
            f.write(f"{key},{value}\n")

    return len(missing_keys)


def main():
    # Paths
    script_dir = Path(__file__).parent
    lang_dir = script_dir.parent.parent / "AwowLab" / "data" / "lang"
    source_file = lang_dir / "en_US.csv"

    # Parse arguments
    dry_run = "--dry-run" in sys.argv
    specific_locales = [arg for arg in sys.argv[1:] if not arg.startswith("-")]

    if specific_locales:
        locales_to_sync = [loc for loc in specific_locales if loc in LOCALES]
        if not locales_to_sync:
            print(f"Error: Invalid locale(s). Valid options: {', '.join(LOCALES)}")
            sys.exit(1)
    else:
        locales_to_sync = LOCALES

    print("AwowLab Localization - Sync Missing Strings")
    print("=" * 50)
    if dry_run:
        print("DRY RUN - No files will be modified\n")

    # Load source file
    print(f"Reading source: {source_file.name}")
    if not source_file.exists():
        print(f"Error: Source file not found: {source_file}")
        sys.exit(1)

    source_keys, source_structure = parse_csv(source_file)
    print(f"Found {len(source_keys)} strings in en_US.csv\n")

    # Process each locale
    total_added = 0
    for locale in locales_to_sync:
        target_file = lang_dir / f"{locale}.csv"

        if not target_file.exists():
            print(f"[{locale}] File not found - skipping")
            continue

        target_keys, _ = parse_csv(target_file)
        missing = find_missing_keys(source_keys, target_keys)

        if not missing:
            print(f"[{locale}] Up to date ({len(target_keys)} strings)")
        else:
            print(f"[{locale}] Missing {len(missing)} strings")
            added = add_missing_to_file(
                target_file, source_keys, source_structure, missing, dry_run
            )
            total_added += added
            if not dry_run:
                print(f"  Added {added} strings")

    print("\n" + "=" * 50)
    if dry_run:
        print(f"Would add {total_added} total strings")
    else:
        print(f"Synced {total_added} total strings")

    if total_added > 0 and not dry_run:
        print("\nNext steps:")
        print("  1. Run translate_all.py to translate new strings")
        print("  2. Or manually translate the 'NEW - Needs Translation' section")


if __name__ == "__main__":
    main()
