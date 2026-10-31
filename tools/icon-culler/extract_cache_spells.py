"""Extract spell IDs from WCL cache (trash and all combat abilities)."""

import json
from pathlib import Path
from config import WCL_CACHE_DIR, OUTPUT_DIR


def extract_spells_from_cache(cache_dir):
    """Extract all abilityGameID from cached WCL events."""
    spell_ids = set()
    cache_path = Path(cache_dir)

    if not cache_path.exists():
        print(f"  Warning: Cache directory not found: {cache_path}")
        return spell_ids

    page_files = list(cache_path.glob('*/fight*/page*.json'))
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
        except (json.JSONDecodeError, IOError) as e:
            print(f"  Warning: Error reading {page_file}: {e}")
            continue

    return spell_ids


def main():
    output_dir = Path(OUTPUT_DIR)
    output_dir.mkdir(parents=True, exist_ok=True)

    print("Extracting spell IDs from WCL cache...")

    spell_ids = extract_spells_from_cache(WCL_CACHE_DIR)
    print(f"  Found {len(spell_ids)} unique spell IDs from cache")

    # Save results
    output_file = output_dir / 'cache_spell_ids.txt'
    with open(output_file, 'w') as f:
        for sid in sorted(spell_ids):
            f.write(f"{sid}\n")

    print(f"  Saved to {output_file}")
    return spell_ids


if __name__ == '__main__':
    main()
