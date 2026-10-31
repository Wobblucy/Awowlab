"""Main icon culler script - combines all sources and copies required icons."""

import csv
import shutil
from pathlib import Path
from config import (
    RECENT_SPELL_THRESHOLD,
    ICON_TABLE_CSV,
    SOURCE_ICONS_DIR,
    OUTPUT_DIR,
    REQUIRED_ICONS_FILE
)


def load_spell_ids(*files):
    """Load and combine spell IDs from multiple files."""
    all_ids = set()
    for filepath in files:
        filepath = Path(filepath)
        if filepath.exists():
            with open(filepath, 'r') as f:
                for line in f:
                    line = line.strip()
                    if line.isdigit():
                        all_ids.add(int(line))
            print(f"  Loaded {len(all_ids)} spell IDs from {filepath.name}")
    return all_ids


def load_icon_mapping(csv_path):
    """Load spell_icon_table.csv -> {spell_id: icon_filename}."""
    mapping = {}
    csv_path = Path(csv_path)

    if not csv_path.exists():
        print(f"  Warning: Icon table not found: {csv_path}")
        return mapping

    with open(csv_path, 'r', encoding='utf-8') as f:
        reader = csv.reader(f)
        header = next(reader, None)  # Skip header

        for row in reader:
            if len(row) >= 4:
                try:
                    spell_id = int(row[0])
                    icon_filename = row[3].strip()

                    # Convert .blp to .png and lowercase
                    if icon_filename and 'NOT_FOUND' not in icon_filename.upper():
                        # Lowercase first, then replace .blp with .png
                        icon_filename = icon_filename.lower().replace('.blp', '.png')
                        mapping[spell_id] = icon_filename
                except (ValueError, IndexError):
                    continue

    print(f"  Loaded {len(mapping)} spell-to-icon mappings")
    return mapping


def get_recent_spell_ids(icon_mapping, threshold):
    """Get spell IDs above threshold (recent additions)."""
    recent = {sid for sid in icon_mapping.keys() if sid >= threshold}
    print(f"  Found {len(recent)} recent spells (ID >= {threshold})")
    return recent


def load_required_icons(filepath):
    """Load icon filenames that must always be included (e.g., spec icons)."""
    icons = set()
    path = Path(filepath)
    if not path.exists():
        print(f"  Warning: Required icons file not found: {filepath}")
        return icons

    with open(path, 'r') as f:
        for line in f:
            line = line.strip()
            # Skip empty lines and comments
            if line and not line.startswith('#'):
                icons.add(line.lower())

    print(f"  Loaded {len(icons)} required icons from {path.name}")
    return icons


def cull_icons(spell_ids, icon_mapping, source_dir, output_dir, extra_icons=None):
    """Copy only required icons to output directory."""
    output_path = Path(output_dir)
    output_path.mkdir(parents=True, exist_ok=True)

    source_path = Path(source_dir)
    if not source_path.exists():
        print(f"  Warning: Source icons directory not found: {source_path}")
        return 0, [], 0, {}

    # Get unique icon filenames from spell IDs, remembering which spells asked
    # for each one so a missing file can be traced back to what needs it.
    icons_from_spells = set()
    spell_ids_for_icon = {}
    for spell_id in spell_ids:
        if spell_id in icon_mapping:
            name = icon_mapping[spell_id]
            icons_from_spells.add(name)
            spell_ids_for_icon.setdefault(name, set()).add(spell_id)

    print(f"  {len(icons_from_spells)} icons from spell IDs")

    # Add required icons by filename (e.g., spec icons)
    if extra_icons:
        print(f"  {len(extra_icons)} extra icons (spec icons, etc.)")

    # Combine all required icons
    required_icons = icons_from_spells | (extra_icons or set())
    print(f"  {len(required_icons)} total unique icon files required")

    # Copy icons. Missing ones are collected by name rather than counted: a
    # count tells you something is wrong without telling you what to fix, and
    # every missing icon is a blank square somewhere in the UI.
    copied = 0
    missing = []

    for icon_name in sorted(required_icons):
        src = source_path / icon_name
        if src.exists():
            dst = output_path / icon_name
            shutil.copy2(src, dst)
            copied += 1
        else:
            missing.append(icon_name)

    if missing:
        print(f"\n  {len(missing)} icon(s) required but not in {source_path}:")
        for name in missing:
            print(f"    {name}")

    return copied, missing, len(required_icons), spell_ids_for_icon


def generate_report(boss_ids, cache_ids, class_ids, recent_ids, extra_icons, all_ids,
                    copied, missing, total, spell_ids_for_icon=None):
    """Generate a summary report."""
    spell_ids_for_icon = spell_ids_for_icon or {}
    output_dir = Path(OUTPUT_DIR)
    report_file = output_dir / 'cull_report.txt'

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
        f.write("Extra Icons (by filename):\n")
        f.write(f"  Spec/class icons:         {len(extra_icons):>8}\n\n")
        f.write("Icon Culling Results:\n")
        f.write(f"  Icons required:           {total:>8}\n")
        f.write(f"  Icons copied:             {copied:>8}\n")
        f.write(f"  Icons missing:            {len(missing):>8}\n")

        if missing:
            f.write("\nMissing icons\n")
            f.write("-------------\n")
            f.write("Named in the spell data but absent from the source folder.\n")
            f.write("Each one renders as a blank square. Either the art is new and\n")
            f.write("needs extracting from the client, or the source pack is stale.\n\n")
            for name in missing:
                spells = sorted(spell_ids_for_icon.get(name, ()))[:6]
                if spells:
                    shown = ', '.join(str(s) for s in spells)
                    more = ' ...' if len(spell_ids_for_icon.get(name, ())) > 6 else ''
                    f.write(f"  {name}\n      wanted by spell {shown}{more}\n")
                else:
                    f.write(f"  {name}\n      required by filename (spec/class icon)\n")

    print(f"  Report saved to {report_file}")


def main():
    output_dir = Path(OUTPUT_DIR)
    output_dir.mkdir(parents=True, exist_ok=True)

    print("\n=== Icon Pack Culler ===\n")

    # Run individual extractors if their outputs don't exist
    boss_file = output_dir / 'boss_spell_ids.txt'
    cache_file = output_dir / 'cache_spell_ids.txt'
    class_file = output_dir / 'class_spell_ids.txt'

    if not boss_file.exists():
        print("Running DB2 extractor...")
        from extract_db2_spells import main as extract_db2
        extract_db2()
        print()

    if not cache_file.exists():
        print("Running cache extractor...")
        from extract_cache_spells import main as extract_cache
        extract_cache()
        print()

    if not class_file.exists():
        print("Running spell data reader...")
        from read_spell_data import main as read_spells
        read_spells()
        print()

    # Load all spell ID sources
    print("Loading spell ID files...")
    boss_ids = load_spell_ids(boss_file)
    cache_ids = load_spell_ids(cache_file)
    class_ids = load_spell_ids(class_file)

    # Load icon mapping
    print("\nLoading icon mapping...")
    icon_mapping = load_icon_mapping(ICON_TABLE_CSV)

    # Get recent spells
    print("\nFinding recent spells...")
    recent_ids = get_recent_spell_ids(icon_mapping, RECENT_SPELL_THRESHOLD)

    # Load required icons by filename (spec icons, etc.)
    print("\nLoading required icons by filename...")
    extra_icons = load_required_icons(REQUIRED_ICONS_FILE)

    # Combine all sources
    all_spell_ids = boss_ids | cache_ids | class_ids | recent_ids

    print(f"\n=== Summary ===")
    print(f"Boss abilities:     {len(boss_ids):>8}")
    print(f"Cache abilities:    {len(cache_ids):>8}")
    print(f"Class abilities:    {len(class_ids):>8}")
    print(f"Recent spells:      {len(recent_ids):>8}")
    print(f"Total unique IDs:   {len(all_spell_ids):>8}")
    print(f"Extra icons:        {len(extra_icons):>8}")

    # Save combined list
    all_ids_file = output_dir / 'all_spell_ids.txt'
    with open(all_ids_file, 'w') as f:
        for sid in sorted(all_spell_ids):
            f.write(f"{sid}\n")
    print(f"\nSaved combined spell IDs to {all_ids_file}")

    # Cull icons
    print(f"\nCulling icons...")
    copied, missing, total, spell_ids_for_icon = cull_icons(
        all_spell_ids,
        icon_mapping,
        SOURCE_ICONS_DIR,
        OUTPUT_DIR,
        extra_icons
    )

    print(f"\n=== Results ===")
    print(f"Icons copied:  {copied}")
    print(f"Icons missing: {len(missing)}")
    print(f"Total needed:  {total}")

    # Generate report
    generate_report(boss_ids, cache_ids, class_ids, recent_ids, extra_icons, all_spell_ids,
                    copied, missing, total, spell_ids_for_icon)

    # Calculate size
    if output_dir.exists():
        total_size = sum(f.stat().st_size for f in output_dir.glob('*.png') if f.is_file())
        print(f"\nCulled pack size: {total_size / (1024*1024):.1f} MB")

    print("\nDone!")


if __name__ == '__main__':
    main()
