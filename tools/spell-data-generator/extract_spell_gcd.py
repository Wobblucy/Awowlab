#!/usr/bin/env python3
"""
Spell GCD/Cooldown/CastTime Extractor for AwowLab
Extracts spell_id, name, GCD, cooldown, and cast time data from WoW db2 text exports.

Usage:
  python extract_spell_gcd.py <db2_folder>
  python extract_spell_gcd.py              (reads AWOWLAB_DB2_DIR)

Without a folder and without AWOWLAB_DB2_DIR set, it prompts for one
(double-click runs it this way), defaulting to rawdb2/<version>.

Required db2 files (either naming convention):
  - spellname.txt or spellname_export.txt
  - spellcooldowns.txt or spellcooldowns_export.txt
  - spellcasttimes.txt or spellcasttimes_export.txt
  - spellmisc.txt or spellmisc_export.txt
  - skilllineability.txt or skilllineability_export.txt

Output: CSV files per class + combined all_spells.csv in output/ folder
"""

import os
import sys
import csv
import re
from pathlib import Path
from collections import defaultdict

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from db2_layouts import (read_layout_hash, LayoutMismatch, DB2_DIR_ENV,
                         default_db2_dir, resolve_db2_dir)

# Class skill line IDs from skillline_export.txt
CLASS_SKILL_LINES = {
    795: "Hunter",
    796: "DeathKnight",
    798: "Druid",
    800: "Paladin",
    804: "Priest",
    829: "Monk",
    840: "Warrior",
    849: "Warlock",
    904: "Mage",
    921: "Rogue",
    924: "Shaman",
    1848: "DemonHunter",
    2810: "Evoker",
}


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


def parse_spellname(filepath: Path) -> dict[int, str]:
    """Parse spellname.txt -> {spell_id: name}"""
    spells = {}
    with open(filepath, 'r', encoding='utf-8-sig') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('Layout:'):
                continue
            # Format: "ID: Name"
            if ':' in line:
                parts = line.split(':', 1)
                try:
                    spell_id = int(parts[0].strip())
                    name = parts[1].strip()
                    spells[spell_id] = name
                except (ValueError, IndexError):
                    continue
    return spells


def parse_spellcooldowns(filepath: Path) -> dict[int, tuple[int, int]]:
    """
    Parse spellcooldowns.txt -> {spell_id: (gcd_ms, cooldown_ms)}

    Format: ID: DifficultyID, CategoryCooldown, StartRecoveryCategory, StartRecoveryTime, ??, SpellID
    - StartRecoveryTime (field 4) = GCD in milliseconds
    - StartRecoveryCategory (field 3) = Cooldown in milliseconds
    - CategoryCooldown (field 2) = Category cooldown (shared CD)
    """
    cooldowns = {}
    with open(filepath, 'r', encoding='utf-8-sig') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('Layout:'):
                continue
            if ':' in line:
                parts = line.split(':', 1)
                try:
                    fields = parts[1].strip().split(',')
                    if len(fields) >= 6:
                        spell_id = int(fields[5].strip())
                        category_cd = int(fields[1].strip()) if fields[1].strip() else 0
                        cooldown = int(fields[2].strip()) if fields[2].strip() else 0
                        gcd = int(fields[3].strip()) if fields[3].strip() else 0

                        # Use whichever cooldown is larger (category or direct)
                        actual_cd = max(category_cd, cooldown)
                        cooldowns[spell_id] = (gcd, actual_cd)
                except (ValueError, IndexError):
                    continue
    return cooldowns


def parse_spellcasttimes(filepath: Path) -> dict[int, int]:
    """
    Parse spellcasttimes.txt -> {cast_time_id: base_cast_time_ms}

    Format: ID: BaseCastTime, MinCastTime
    We only use BaseCastTime (min is mostly identical in db2)
    """
    cast_times = {}
    with open(filepath, 'r', encoding='utf-8-sig') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('Layout:'):
                continue
            if ':' in line:
                parts = line.split(':', 1)
                try:
                    cast_time_id = int(parts[0].strip())
                    fields = parts[1].strip().split(',')
                    base_cast_time = int(fields[0].strip()) if len(fields) >= 1 and fields[0].strip() else 0
                    cast_times[cast_time_id] = base_cast_time
                except (ValueError, IndexError):
                    continue
    return cast_times


def parse_spellmisc(filepath: Path) -> dict[int, int]:
    """
    Parse spellmisc.txt -> {spell_id: cast_time_id}

    Format: ID: [Attributes...],f0,CastTimeID,f2,f3,f4,f5,f6,f7,f8,IconID,f10,f11,f12,f13,f14,SpellID
    - CastTimeID is field 1 after the array (0-indexed: 1)
    - SpellID is the last field (field 15 after array, 0-indexed)
    """
    spell_cast_time_ids = {}
    with open(filepath, 'r', encoding='utf-8-sig') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('Layout:'):
                continue
            if ':' in line:
                parts = line.split(':', 1)
                try:
                    content = parts[1].strip()

                    # Handle the array at the start: [...],rest
                    # Find the closing bracket and extract fields after it
                    if content.startswith('['):
                        bracket_end = content.find(']')
                        if bracket_end == -1:
                            continue
                        # Skip the array and the comma after it
                        after_array = content[bracket_end + 1:].lstrip(',')
                    else:
                        after_array = content

                    fields = after_array.split(',')
                    if len(fields) >= 16:
                        cast_time_id = int(fields[1].strip()) if fields[1].strip() else 0
                        spell_id = int(fields[15].strip()) if fields[15].strip() else 0
                        if spell_id > 0:
                            spell_cast_time_ids[spell_id] = cast_time_id
                except (ValueError, IndexError):
                    continue
    return spell_cast_time_ids


# skilllineability column positions, which differ by game version.
#
# 12.0:  RaceMask,,,ID,SkillLine,Spell,...   -> SkillLine at 4, Spell at 5
# 12.1:  ,,ID,SkillLine,Spell,...            -> SkillLine at 3, Spell at 4
#
# 12.1 dropped the leading RaceMask field, sliding every column left by one, and
# added a [x,y] array near the end. The field count stays 18 either way, so a
# stale index reads Spell as SkillLine and a flag as Spell -- producing wrong
# class spell lists rather than an error. The layout hash is what distinguishes
# them; see tools/db2_layouts.json.
SKILLLINEABILITY_COLUMNS = {
    "c070825e": {"skillline": 4, "spell": 5},  # 12.0
    "f98aa48e": {"skillline": 3, "spell": 4},  # 12.1
}


def parse_skilllineability(filepath: Path) -> dict[int, set[int]]:
    """
    Parse a skilllineability export -> {skillline_id: set of spell_ids}

    Column positions are chosen from the export's own layout hash, so a dump
    whose shape this parser has never seen fails loudly instead of silently
    reading the wrong fields.
    """
    layout_hash = read_layout_hash(filepath)
    columns = SKILLLINEABILITY_COLUMNS.get(layout_hash)
    if columns is None:
        raise LayoutMismatch(
            f"\nskilllineability: unrecognized layout hash {layout_hash}\n"
            f"  file: {filepath}\n"
            f"  known: {', '.join(sorted(SKILLLINEABILITY_COLUMNS))}\n\n"
            f"  This table's columns move between game versions. Dump a row, find\n"
            f"  the SkillLine and Spell positions, and add them to\n"
            f"  SKILLLINEABILITY_COLUMNS above before trusting this parse.\n"
        )

    skillline_idx = columns["skillline"]
    spell_idx = columns["spell"]
    needed = max(skillline_idx, spell_idx) + 1

    abilities = defaultdict(set)
    with open(filepath, 'r', encoding='utf-8-sig') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('Layout:'):
                continue
            if ':' in line:
                parts = line.split(':', 1)
                try:
                    fields = parts[1].strip().split(',')
                    if len(fields) >= needed:
                        skillline_id = int(fields[skillline_idx].strip())
                        spell_id = int(fields[spell_idx].strip())
                        abilities[skillline_id].add(spell_id)
                except (ValueError, IndexError):
                    continue
    return abilities


def main():
    script_dir = Path(__file__).parent
    interactive_mode = len(sys.argv) <= 1 and not os.environ.get(DB2_DIR_ENV)

    print("=" * 60)
    print("Spell GCD/Cooldown/CastTime Extractor for AwowLab")
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
    required_files = [
        "spellname",
        "spellcooldowns",
        "spellcasttimes",
        "spellmisc",
        "skilllineability",
    ]

    file_paths = {}
    print()
    print("Checking required files...")

    for base_name in required_files:
        filepath = find_file(db2_path, base_name)
        if filepath is None:
            print(f"  ERROR: Missing required file: {base_name}.txt or {base_name}_export.txt")
            if interactive_mode:
                input("Press Enter to exit...")
            sys.exit(1)
        file_paths[base_name] = filepath
        print(f"  Found: {filepath.name}")

    print()
    print("Parsing db2 files...")

    # Parse all data
    print("  - Parsing spell names...")
    spell_names = parse_spellname(file_paths["spellname"])
    print(f"    Found {len(spell_names)} spells")

    print("  - Parsing spell cooldowns...")
    spell_cooldowns = parse_spellcooldowns(file_paths["spellcooldowns"])
    print(f"    Found {len(spell_cooldowns)} cooldown entries")

    print("  - Parsing spell cast times lookup...")
    cast_time_lookup = parse_spellcasttimes(file_paths["spellcasttimes"])
    print(f"    Found {len(cast_time_lookup)} cast time entries")

    print("  - Parsing spell misc (cast time IDs)...")
    spell_cast_time_ids = parse_spellmisc(file_paths["spellmisc"])
    print(f"    Found {len(spell_cast_time_ids)} spell->castTimeID mappings")

    print("  - Parsing skill line abilities...")
    skill_abilities = parse_skilllineability(file_paths["skilllineability"])
    print(f"    Found {len(skill_abilities)} skill lines")

    # Build spell_id -> cast_time_ms lookup
    spell_cast_times = {}
    for spell_id, cast_time_id in spell_cast_time_ids.items():
        cast_time_ms = cast_time_lookup.get(cast_time_id, 0)
        spell_cast_times[spell_id] = cast_time_ms

    print(f"    Resolved {len(spell_cast_times)} spell cast times")

    # Get output directory
    output_dir = script_dir / "output"
    output_dir.mkdir(exist_ok=True)

    print()
    print(f"Output directory: {output_dir}")
    print()

    # Build class spell data
    class_spells = defaultdict(list)
    all_spells = []

    for skillline_id, class_name in CLASS_SKILL_LINES.items():
        spell_ids = skill_abilities.get(skillline_id, set())
        for spell_id in spell_ids:
            name = spell_names.get(spell_id, "")
            gcd_ms, cooldown_ms = spell_cooldowns.get(spell_id, (0, 0))
            cast_time_ms = spell_cast_times.get(spell_id, 0)

            spell_data = {
                'spell_id': spell_id,
                'name': name,
                'gcd_ms': gcd_ms,
                'cooldown_ms': cooldown_ms,
                'cast_time_ms': cast_time_ms,
                'class': class_name,
            }
            class_spells[class_name].append(spell_data)
            all_spells.append(spell_data)

    # Also include spells with cooldown data that aren't class-specific
    class_spell_ids = set()
    for spells in class_spells.values():
        for spell in spells:
            class_spell_ids.add(spell['spell_id'])

    for spell_id, (gcd_ms, cooldown_ms) in spell_cooldowns.items():
        if spell_id not in class_spell_ids:
            name = spell_names.get(spell_id, "")
            cast_time_ms = spell_cast_times.get(spell_id, 0)
            spell_data = {
                'spell_id': spell_id,
                'name': name,
                'gcd_ms': gcd_ms,
                'cooldown_ms': cooldown_ms,
                'cast_time_ms': cast_time_ms,
                'class': 'General',
            }
            class_spells['General'].append(spell_data)
            all_spells.append(spell_data)

    # Write CSV files
    fieldnames = ['spell_id', 'name', 'gcd_ms', 'cooldown_ms', 'cast_time_ms']

    # Per-class files
    for class_name, spells in sorted(class_spells.items()):
        filename = output_dir / f"{class_name.lower()}_spells.csv"
        spells_sorted = sorted(spells, key=lambda x: x['spell_id'])

        with open(filename, 'w', newline='', encoding='utf-8') as f:
            writer = csv.DictWriter(f, fieldnames=fieldnames, extrasaction='ignore')
            writer.writeheader()
            writer.writerows(spells_sorted)

        print(f"  Wrote {filename.name}: {len(spells)} spells")

    # Combined file
    all_filename = output_dir / "all_spells.csv"
    all_spells_sorted = sorted(all_spells, key=lambda x: x['spell_id'])

    with open(all_filename, 'w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=['spell_id', 'name', 'gcd_ms', 'cooldown_ms', 'cast_time_ms', 'class'], extrasaction='ignore')
        writer.writeheader()
        writer.writerows(all_spells_sorted)

    print(f"  Wrote {all_filename.name}: {len(all_spells)} total spells")

    print()
    print("=" * 60)
    print("Extraction complete!")
    print("=" * 60)

    if interactive_mode:
        input("Press Enter to exit...")


if __name__ == "__main__":
    main()
