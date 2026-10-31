#!/usr/bin/env python3
"""
List required BLP files for specific map ID(s).
Helps you know which files to extract before stitching.

Usage:
  python list_required_files.py                    # Interactive mode
  python list_required_files.py 2460               # Single map ID
  python list_required_files.py 2460-2471          # Range of IDs
  python list_required_files.py 2460,2461,2470     # Multiple IDs
  python list_required_files.py 2433,2434 --listfile season_12.2_listfile.csv
                                                   # Also write the fileDataID
                                                   # listfile extract_season_tiles.sh reads

Supports:
  - Single ID: 2460
  - Multiple IDs: 2460,2461,2462
  - Range: 2460-2471
  - Combined: 2460-2465,2470,2471
"""

import sys
import json
import re
from pathlib import Path


def prompt_input(prompt_text: str, default: str = None) -> str:
    """Prompt user for input with optional default."""
    if default:
        user_input = input(f"{prompt_text} [{default}]: ").strip()
        return user_input if user_input else default
    else:
        return input(f"{prompt_text}: ").strip()


def parse_map_ids(input_str: str) -> list:
    """
    Parse map ID input string.
    Supports: single (2460), comma-separated (2460,2461), ranges (2460-2471), or combined.
    Returns sorted list of unique IDs.
    """
    ids = set()

    # Split by comma
    parts = input_str.replace(' ', '').split(',')

    for part in parts:
        if not part:
            continue

        if '-' in part:
            # Range like 2460-2471
            try:
                start, end = part.split('-', 1)
                start_id = int(start)
                end_id = int(end)
                for i in range(min(start_id, end_id), max(start_id, end_id) + 1):
                    ids.add(i)
            except ValueError:
                print(f"  Warning: Invalid range '{part}', skipping")
        else:
            # Single ID
            try:
                ids.add(int(part))
            except ValueError:
                print(f"  Warning: Invalid ID '{part}', skipping")

    return sorted(ids)


def main():
    script_dir = Path(__file__).parent

    # --listfile <path> writes "fileDataID;path" lines for the extractor.
    listfile_path = None
    if '--listfile' in sys.argv:
        i = sys.argv.index('--listfile')
        if i + 1 >= len(sys.argv):
            print("  Error: --listfile needs a file name.")
            sys.exit(1)
        listfile_path = Path(sys.argv[i + 1])
        del sys.argv[i:i + 2]

    interactive_mode = len(sys.argv) <= 1

    if interactive_mode:
        print("=" * 60)
        print("  Required Files Lookup")
        print("=" * 60)
        print()
        print("This tool lists the BLP files needed for specific map ID(s).")
        print()

    # Master JSON file - must be in script directory
    master_json = script_dir / "master_map_alignment_coordinates.json"
    if not master_json.exists():
        print("ERROR: master_map_alignment_coordinates.json not found!")
        print()
        print("Please run generate_master_coordinates.py first to create it.")
        if interactive_mode:
            input("\nPress Enter to exit...")
        sys.exit(1)

    # Load master data
    if interactive_mode:
        print("Loading master coordinates...")
    with open(master_json, 'r', encoding='utf-8') as f:
        master_data = json.load(f)
    if interactive_mode:
        print(f"  Loaded {len(master_data)} maps")

    # Get map IDs from command line or prompt
    if len(sys.argv) > 1:
        id_input = sys.argv[1]
        map_ids = parse_map_ids(id_input)
        if not map_ids:
            print("  Error: No valid map IDs found.")
            sys.exit(1)
    else:
        print()
        print("Enter map ID(s) to look up.")
        print("  Examples: 2460 | 2460,2461,2462 | 2460-2471 | 2460-2465,2470")
        print()

        while True:
            id_input = prompt_input("Map ID(s)")
            if not id_input:
                print("  Error: Please enter at least one map ID.")
                continue

            map_ids = parse_map_ids(id_input)
            if not map_ids:
                print("  Error: No valid map IDs found.")
                continue
            break

    if interactive_mode:
        print()
        print(f"Looking up {len(map_ids)} map ID(s)...")

    # Collect all required files
    all_files = {}  # path -> set of filenames
    file_ids = {}   # (path, filename) -> fileDataID
    maps_found = []
    maps_not_found = []
    maps_no_tiles = []

    for map_id in map_ids:
        map_id_str = str(map_id)

        if map_id_str not in master_data:
            maps_not_found.append(map_id)
            continue

        entry = master_data[map_id_str]
        map_name = entry.get('name', f'Unknown_{map_id}')

        if 'tiles' not in entry or not entry['tiles']:
            maps_no_tiles.append((map_id, map_name))
            continue

        maps_found.append((map_id, map_name, len(entry['tiles'])))

        for tile in entry['tiles']:
            filename = tile.get('filename', '')
            if filename:
                # The folder is the tile name without its position: "ManaforgeRaid"
                # from "ManaforgeRaid_A1.blp", and for names without an underscore,
                # "TempleOfSethraliss" from "TempleOfSethralissA1.blp" and
                # "LightbloomDungeon" from "LightbloomDungeon1.blp".
                stem = filename[:-4] if filename.lower().endswith('.blp') else filename
                base = stem.rsplit('_', 1)[0] if '_' in stem else re.sub(r'[A-Z]?\d+$', '', stem)
                path = f"Interface\\WorldMap\\{base}"

                if path not in all_files:
                    all_files[path] = set()
                all_files[path].add(filename)
                if tile.get('fileDataID'):
                    file_ids[(path, filename)] = tile['fileDataID']

    # Display results
    print()
    print("=" * 60)
    print("  Results")
    print("=" * 60)

    if maps_not_found:
        print()
        print(f"Map IDs NOT FOUND ({len(maps_not_found)}):")
        for mid in maps_not_found:
            print(f"  - {mid}")

    if maps_no_tiles:
        print()
        print(f"Maps with NO TILE DATA ({len(maps_no_tiles)}):")
        for mid, name in maps_no_tiles:
            print(f"  - {mid}: {name}")

    if maps_found:
        print()
        print(f"Maps found ({len(maps_found)}):")
        for mid, name, tile_count in maps_found:
            print(f"  - {mid}: {name} ({tile_count} tiles)")

    if all_files:
        print()
        print("=" * 60)
        print("  Required BLP Files")
        print("=" * 60)

        total_files = sum(len(files) for files in all_files.values())
        print(f"\nTotal files needed: {total_files}")
        print()

        for path in sorted(all_files.keys()):
            files = sorted(all_files[path])
            print(f"Path: {path}\\")
            for filename in files:
                print(f"  {filename}")
            print()

        # Also output as a simple list for easy copying
        print("=" * 60)
        print("  Full Paths (for extraction)")
        print("=" * 60)
        print()
        for path in sorted(all_files.keys()):
            for filename in sorted(all_files[path]):
                print(f"{path}\\{filename}")

        if listfile_path:
            # The extractor takes forward slashes and one "fdid;path" per line.
            lines = sorted(f"{fdid};{path.replace(chr(92), '/')}/{filename}"
                           for (path, filename), fdid in file_ids.items())
            listfile_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
            print()
            print(f"Wrote {len(lines)} entries to {listfile_path}")

        # Ask if user wants to save to file (only in interactive mode)
        if interactive_mode:
            print()
            save = prompt_input("Save file list to text file? (y/n)", default="n")
            if save.lower() in ('y', 'yes'):
                output_file = script_dir / "required_blp_files.txt"
                with open(output_file, 'w', encoding='utf-8') as f:
                    f.write(f"Required BLP files for map IDs: {', '.join(str(m) for m in map_ids)}\n")
                    f.write(f"Total files: {total_files}\n")
                    f.write("=" * 60 + "\n\n")

                    for path in sorted(all_files.keys()):
                        for filename in sorted(all_files[path]):
                            f.write(f"{path}\\{filename}\n")

                print(f"  Saved to: {output_file}")

    if interactive_mode:
        print()
        input("Press Enter to exit...")


if __name__ == '__main__':
    main()
