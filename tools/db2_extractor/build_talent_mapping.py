#!/usr/bin/env python3
"""
Build Talent ID Mapping for WarcraftLogs Integration

This script extracts the mapping from WCL talent IDs (trait node entry IDs) to spell IDs,
then updates the cooldown_modifiers JSON file with the correct talent IDs for WCL matching.

Input:
- traitnodeentry export: Maps trait node entry ID -> spell ID
- AwowLab/data/cooldown_modifiers_{version}.json: Current cooldown modifiers (uses spell IDs)

Output:
- AwowLab/data/cooldown_modifiers_{version}.json: Updated in place with WCL-compatible talent IDs
- output/talent_id_mapping.json: Full mapping for reference

The version defaults to DEFAULT_VERSION in tools/db2_layouts.py; pass --version to
target another.

WCL Data Flow:
1. WCL provides talent ID (trait node entry ID, e.g., 112870)
2. DB2 maps: 112870 -> spell ID 117875
3. We need to reverse this: Find which trait node entry grants spell ID 218612
"""

import json
import sys
from pathlib import Path
from typing import Dict, List, Optional

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from db2_layouts import DEFAULT_VERSION, find_export, verify_layout, resolve_db2_dir


def parse_traitdefinition(db2_dir: Path, version: str = DEFAULT_VERSION) -> Dict[int, int]:
    """
    Parse the traitdefinition export: trait_definition_id -> spell_id

    Columns are OverrideName_lang, OverrideSubtext_lang, OverrideDescription_lang,
    ID, SpellID, OverrideIcon, OverridesSpellID, VisibleSpellID.

    The three leading text columns hold prose that contains commas, so the
    numeric fields cannot be found by counting from the left - they are the last
    five on the row, which holds no matter how many commas the description adds.
    """
    mapping: Dict[int, int] = {}
    file_path = find_export(db2_dir, "traitdefinition")

    if file_path is None:
        print(f"ERROR: no traitdefinition export found in {db2_dir}")
        return mapping

    verify_layout(file_path, "traitdefinition", version)

    with open(file_path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line.strip() or line.startswith("Layout:") or ":" not in line:
                continue

            id_part, data_part = line.split(":", 1)
            fields = data_part.strip().split(",")
            if len(fields) < 5:
                continue

            try:
                definition_id = int(id_part.strip())
                spell_id = int(fields[-4])   # ID, SpellID, Icon, Overrides, Visible
                if spell_id > 0:
                    mapping[definition_id] = spell_id
            except ValueError:
                continue

    print(f"Loaded {len(mapping)} trait definition -> spell ID mappings")
    return mapping


def parse_traitnodeentry(db2_dir: Path, version: str = DEFAULT_VERSION) -> Dict[int, int]:
    """
    Parse the traitnodeentry export to build mapping: trait_node_entry_id -> spell_id

    Format: ID: ID,TraitDefinitionID,MaxRanks,NodeEntryType,TraitSubTreeID
    Example: 112870: 112870,117875,1,2,0

    The second field is a TraitDefinitionID, not a spell id - the spell lives one
    hop further on, in traitdefinition. The two id ranges overlap, so reading
    field 1 as a spell id does not fail loudly: it silently matches unrelated
    spells. Checked against real Arms Warrior talents, field 1 resolved to
    nothing while the definition chain gave Mortal Strike, Overpower and
    Colossus Smash.

    Returns dict: {112870: 117875, ...}
    """
    definition_to_spell = parse_traitdefinition(db2_dir, version)

    mapping = {}
    file_path = find_export(db2_dir, "traitnodeentry")

    if file_path is None:
        print(f"ERROR: no traitnodeentry export found in {db2_dir}")
        print("  looked for traitnodeentry.txt and traitnodeentry_export.txt")
        return mapping

    verify_layout(file_path, "traitnodeentry", version)

    with open(file_path, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("Layout:"):
                continue

            # Parse: "112870: 112870,117875,1,2,0"
            if ':' not in line:
                continue

            id_part, data_part = line.split(':', 1)
            fields = data_part.strip().split(',')

            if len(fields) < 2:
                continue

            try:
                trait_node_entry_id = int(id_part.strip())
                definition_id = int(fields[1])  # TraitDefinitionID, not a spell

                spell_id = definition_to_spell.get(definition_id, 0)
                if spell_id > 0:  # Definitions without a spell are UI-only nodes
                    mapping[trait_node_entry_id] = spell_id
            except ValueError:
                continue

    print(f"Loaded {len(mapping)} trait node entry -> spell ID mappings")
    return mapping


def build_reverse_mapping(forward_mapping: Dict[int, int]) -> Dict[int, List[int]]:
    """
    Build reverse mapping: spell_id -> list of trait_node_entry_ids

    Multiple talents can grant the same spell (e.g., rank 1 vs rank 2)
    """
    reverse = {}
    for trait_entry_id, spell_id in forward_mapping.items():
        if spell_id not in reverse:
            reverse[spell_id] = []
        reverse[spell_id].append(trait_entry_id)

    return reverse


def update_cooldown_modifiers(
    json_path: Path,
    spell_to_talent_mapping: Dict[int, List[int]]
) -> None:
    """
    Update cooldown_modifiers JSON file with WCL-compatible talent IDs

    For each modifier:
    - Keep talent_spell_id (the spell ID) for documentation
    - Add wcl_talent_ids (list of trait node entry IDs that grant this spell)

    A spell missing from this export keeps whatever ids it already had. Some
    entries were mapped from an earlier export and are still correct even when
    the current one no longer lists the talent; overwriting them with an empty
    list would silently drop working WCL matches.
    """
    with open(json_path, 'r') as f:
        data = json.load(f)

    updated_count = 0
    kept_count = 0
    missing_count = 0

    for section in ("resource_modifiers", "flat_modifiers", "percent_modifiers"):
        for modifier in data.get(section, []):
            talent_spell_id = modifier["talent_spell_id"]
            name = modifier.get("talent_name", f"spell {talent_spell_id}")

            # Which WCL talent ids grant this spell, per the current export
            wcl_talent_ids = spell_to_talent_mapping.get(talent_spell_id, [])

            if wcl_talent_ids:
                if modifier.get("wcl_talent_ids") != wcl_talent_ids:
                    modifier["wcl_talent_ids"] = wcl_talent_ids
                    updated_count += 1
                    print(f"  {name} (spell {talent_spell_id}) -> WCL talents {wcl_talent_ids}")
            elif modifier.get("wcl_talent_ids"):
                kept_count += 1
                print(f"  KEEP: {name} (spell {talent_spell_id}) not in this export, "
                      f"keeping existing {modifier['wcl_talent_ids']}")
            else:
                modifier.setdefault("wcl_talent_ids", [])
                missing_count += 1
                print(f"  WARNING: {name} (spell {talent_spell_id}) - NO WCL TALENT FOUND")

    # Write back
    with open(json_path, 'w') as f:
        json.dump(data, f, indent=2)

    print(f"\nUpdated {updated_count} modifiers with WCL talent IDs")
    if kept_count:
        print(f"Kept {kept_count} existing mappings not present in this export")
    print(f"{missing_count} modifiers have no WCL talent mapping (may be old/removed talents)")


def save_full_mapping(
    output_path: Path,
    forward_mapping: Dict[int, int],
    reverse_mapping: Dict[int, List[int]]
) -> None:
    """Save full mapping for reference"""
    data = {
        "_comment": "Full talent ID mapping for WarcraftLogs integration",
        "trait_node_entry_to_spell": {
            str(k): v for k, v in sorted(forward_mapping.items())
        },
        "spell_to_trait_node_entries": {
            str(k): v for k, v in sorted(reverse_mapping.items())
        }
    }

    with open(output_path, 'w') as f:
        json.dump(data, f, indent=2)

    print(f"\nSaved full mapping to {output_path}")


def main():
    # Pull --version out before reading the positional argument.
    version = DEFAULT_VERSION
    if "--version" in sys.argv:
        idx = sys.argv.index("--version")
        if idx + 1 >= len(sys.argv):
            print("Error: --version needs a value, e.g. --version 12.1")
            sys.exit(1)
        version = sys.argv[idx + 1]
        del sys.argv[idx:idx + 2]

    # The export folder is optional; without it AWOWLAB_DB2_DIR or rawdb2/<version> is used.
    db2_dir = resolve_db2_dir(sys.argv[1] if len(sys.argv) > 1 else None, version)

    script_dir = Path(__file__).resolve().parent
    repo_root = script_dir.parent.parent  # repo_root/tools/db2_extractor -> repo_root

    print(f"Building talent ID mapping for WarcraftLogs integration (version {version})...\n")

    # Step 1: Parse trait node entry → spell ID mapping
    print("[1/4] Parsing traitnodeentry_export.txt...")
    forward_mapping = parse_traitnodeentry(db2_dir, version)

    if not forward_mapping:
        print("ERROR: No mappings found. Check DB2 file format.")
        sys.exit(1)

    # Step 2: Build reverse mapping
    print("\n[2/4] Building reverse mapping (spell -> talent IDs)...")
    reverse_mapping = build_reverse_mapping(forward_mapping)
    print(f"  {len(reverse_mapping)} unique spells can be granted by talents")

    # Step 3: Update cooldown_modifiers JSON
    modifiers_name = f"cooldown_modifiers_{version}.json"
    print(f"\n[3/4] Updating {modifiers_name}...")
    json_path = repo_root / "AwowLab" / "data" / modifiers_name

    if not json_path.exists():
        print(f"ERROR: {json_path} not found")
        sys.exit(1)

    update_cooldown_modifiers(json_path, reverse_mapping)

    # Step 4: Save full mapping for reference
    print("\n[4/4] Saving full mapping...")
    output_dir = script_dir / "output"
    output_dir.mkdir(exist_ok=True)
    save_full_mapping(output_dir / "talent_id_mapping.json", forward_mapping, reverse_mapping)

    print(f"\nDone! {modifiers_name} now has wcl_talent_ids field.")
    print("  The C++ code should use wcl_talent_ids for WCL talent matching.")


if __name__ == "__main__":
    main()
