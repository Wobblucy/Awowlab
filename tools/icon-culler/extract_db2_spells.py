"""Extract boss ability spell IDs from DB2 JournalEncounterSection."""

import re
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from db2_layouts import find_export
from config import TARGET_INSTANCES, DB2_DIR, OUTPUT_DIR


def extract_encounter_ids(encounter_file, target_instances):
    """Get encounter IDs for target instances."""
    encounter_ids = set()

    with open(encounter_file, 'r', encoding='utf-8') as f:
        for line in f:
            if line.startswith('Layout:'):
                continue

            # Format: ID: Name,Desc,[coords],CreatureID,InstanceID,...
            # Extract encounter ID from start of line (before first colon)
            colon_idx = line.find(':')
            if colon_idx == -1:
                continue

            try:
                encounter_id = int(line[:colon_idx].strip())
            except ValueError:
                continue

            # Find the coordinates pattern [x,y] and get fields after it
            # The instance ID is typically 2 fields after the coords
            bracket_start = line.find('[')
            bracket_end = line.find(']', bracket_start)

            if bracket_start == -1 or bracket_end == -1:
                continue

            # Get everything after the closing bracket
            after_coords = line[bracket_end + 1:].strip()
            if after_coords.startswith(','):
                after_coords = after_coords[1:]

            # Split the remaining fields
            remaining_parts = after_coords.split(',')

            # Instance ID is the second field after coords (CreatureID, InstanceID, ...)
            if len(remaining_parts) >= 2:
                instance_id_str = remaining_parts[1].strip()
                if instance_id_str.isdigit():
                    instance_id = int(instance_id_str)
                    if instance_id in target_instances:
                        encounter_ids.add(encounter_id)

    return encounter_ids


def extract_spell_ids_from_sections(section_file, encounter_ids):
    """Extract spell IDs from section descriptions for target encounters."""
    spell_ids = set()

    # Patterns for spell references in descriptions
    # $74986s1, $74986A1, $74986d, etc.
    dollar_pattern = re.compile(r'\$(\d+)[sAdtoi\d]')
    # |Hspell:1255702|h[Spell Name]|h|r
    hyperlink_pattern = re.compile(r'\|Hspell:(\d+)\|h')

    with open(section_file, 'r', encoding='utf-8') as f:
        for line in f:
            if line.startswith('Layout:'):
                continue

            # Parse encounter ID from the section line
            # Two formats exist:
            # 1. Section header: "33965: Section 13,,2733,59,0,0,33972,2,0,0,1251583,0,0,16,-1"
            #    - Position 2 = encounter ID, Position 10 = spell ID
            # 2. Description: "$bullet;|Hspell:ID|h...,2733,3,..."
            #    - Encounter ID somewhere in the line, spell IDs in hyperlinks

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
                # Extract spell IDs from hyperlinks in the entire line
                for match in dollar_pattern.findall(line):
                    spell_ids.add(int(match))
                for match in hyperlink_pattern.findall(line):
                    spell_ids.add(int(match))

                # Also check position 10 for spell ID (section header format)
                # Format: ID: Name,,EncounterID,...,SpellID,...
                if len(parts) >= 11:
                    spell_id_str = parts[10].strip()
                    if spell_id_str.isdigit():
                        spell_id = int(spell_id_str)
                        # Only add if it looks like a valid spell ID (> 1000)
                        if spell_id > 1000:
                            spell_ids.add(spell_id)

    return spell_ids


def main():
    db2_dir = Path(DB2_DIR)
    output_dir = Path(OUTPUT_DIR)
    output_dir.mkdir(parents=True, exist_ok=True)

    print("Extracting boss spell IDs from DB2...")

    # Step 1: Get encounter IDs for target instances
    encounter_file = find_export(db2_dir, 'journalencounter')
    section_file = find_export(db2_dir, 'journalencountersection')
    if encounter_file is None or section_file is None:
        print(f"ERROR: journalencounter/journalencountersection exports not found in {db2_dir}")
        return set()

    encounter_ids = extract_encounter_ids(encounter_file, TARGET_INSTANCES)
    print(f"  Found {len(encounter_ids)} encounters in target instances")

    # Step 2: Extract spell IDs from sections
    spell_ids = extract_spell_ids_from_sections(section_file, encounter_ids)
    print(f"  Found {len(spell_ids)} unique boss spell IDs")

    # Save results
    output_file = output_dir / 'boss_spell_ids.txt'
    with open(output_file, 'w') as f:
        for sid in sorted(spell_ids):
            f.write(f"{sid}\n")

    print(f"  Saved to {output_file}")
    return spell_ids


if __name__ == '__main__':
    main()
