#!/usr/bin/env python3
"""
Generate master_map_alignment_coordinates.json from DB2 export files.
Creates the complete mapping for ALL maps in the database.

This is stage 1 of the world-bounds pipeline. Stage 2 is extract_map_bounds.py,
which flattens this JSON into AwowLab/PNG/Worldmaps/map_world_bounds.csv (the
file the app loads at runtime). See howtobuildWorldMaps.md for the full flow.

Usage:
  python generate_master_coordinates.py <db2_folder>
  python generate_master_coordinates.py    (reads AWOWLAB_DB2_DIR)

Without a folder and without AWOWLAB_DB2_DIR set, it prompts for one
(double-click runs it this way), defaulting to rawdb2/<version>.

Required files in db2 folder:
  - uimap.txt (or uimap_export.txt)
  - uimapxmapart.txt (or uimapxmapart_export.txt)
  - uimaparttile.txt (or uimaparttile_export.txt)
  - manifestinterfacedata.txt (or manifestinterfacedata_export.txt)
  - uimapassignment.txt (optional but recommended)
"""

import os
import re
import sys
import json
from pathlib import Path
from collections import defaultdict

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from db2_layouts import DB2_DIR_ENV, default_db2_dir, resolve_db2_dir


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


def prompt_path(prompt_text: str, default: Path = None, must_exist: bool = True, is_file: bool = False) -> Path:
    """Prompt user for a path with optional default."""
    while True:
        if default:
            user_input = input(f"{prompt_text} [{default}]: ").strip().strip('"\'')
            if not user_input:
                path = default
            else:
                path = Path(user_input)
        else:
            user_input = input(f"{prompt_text}: ").strip().strip('"\'')
            if not user_input:
                print("  Error: Path is required.")
                continue
            path = Path(user_input)

        if must_exist:
            if is_file and not path.is_file():
                print(f"  Error: File not found: {path}")
                continue
            elif not is_file and not path.is_dir():
                print(f"  Error: Directory not found: {path}")
                continue

        return path


def parse_uimap(filepath: Path) -> dict:
    """Parse uimap.txt to get map names and IDs."""
    maps = {}
    with open(filepath, 'r', encoding='utf-8-sig') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('Layout:'):
                continue
            match = re.match(r'^(\d+):\s*(.+)', line)
            if match:
                map_id = int(match.group(1))
                rest = match.group(2)
                name = rest.split(',')[0].strip()
                maps[map_id] = name
    return maps


def parse_uimapxmapart(filepath: Path) -> dict:
    """Parse uimapxmapart.txt to link UIMap IDs to UiMapArt IDs."""
    mappings = {}
    with open(filepath, 'r', encoding='utf-8-sig') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('Layout:'):
                continue
            match = re.match(r'^(\d+):\s*(\d+),(\d+),(\d+)', line)
            if match:
                floor_index = int(match.group(2))
                uimapart_id = int(match.group(3))
                uimap_id = int(match.group(4))
                mappings[uimap_id] = (floor_index, uimapart_id)
    return mappings


def parse_uimaparttile(filepath: Path) -> dict:
    """Parse uimaparttile.txt to get tile grid positions and FileDataIDs."""
    tiles = defaultdict(list)
    with open(filepath, 'r', encoding='utf-8-sig') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('Layout:'):
                continue
            match = re.match(r'^(\d+):\s*(\d+),(\d+),(\d+),(\d+),(\d+)', line)
            if match:
                row = int(match.group(2))
                col = int(match.group(3))
                layer = int(match.group(4))
                file_data_id = int(match.group(5))
                uimapart_id = int(match.group(6))
                tiles[uimapart_id].append((row, col, layer, file_data_id))
    return dict(tiles)


def parse_manifest(filepath: Path) -> dict:
    """Parse manifestinterfacedata.txt to map FileDataIDs to filenames."""
    manifest = {}
    with open(filepath, 'r', encoding='utf-8-sig') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('Layout:'):
                continue
            match = re.match(r'^(\d+):\s*([^,]*),(.+)', line)
            if match:
                file_data_id = int(match.group(1))
                path = match.group(2).strip()
                filename = match.group(3).strip()
                manifest[file_data_id] = {'path': path, 'filename': filename}
    return manifest


def parse_uimapassignment(filepath: Path) -> dict:
    """Parse uimapassignment.txt to get world coordinate bounds."""
    assignments = defaultdict(list)
    pattern = re.compile(
        r'^(\d+):\s*'
        r'\[([^\]]+)\],\[([^\]]+)\],'
        r'\[([^\]]+)\],'
        r'(\d+),(\d+),'
        r'(\d+),(\d+)'
    )
    with open(filepath, 'r', encoding='utf-8-sig') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('Layout:'):
                continue
            match = pattern.match(line)
            if match:
                assignment_id = int(match.group(1))
                uv_min = match.group(2)
                uv_max = match.group(3)
                world_bounds_str = match.group(4)
                uimap_id = int(match.group(6))
                order_index = int(match.group(7))
                map_id = int(match.group(8))

                bounds = world_bounds_str.split(',')
                if len(bounds) >= 6:
                    bounds_dict = {
                        'minX': float(bounds[0]),
                        'minY': float(bounds[1]),
                        'minZ': float(bounds[2]),
                        'maxX': float(bounds[3]),
                        'maxY': float(bounds[4]),
                        'maxZ': float(bounds[5]),
                        'orderIndex': order_index,
                        'mapID': map_id,
                        'assignmentID': assignment_id
                    }
                    uv_min_parts = uv_min.split(',')
                    uv_max_parts = uv_max.split(',')
                    if len(uv_min_parts) >= 2 and len(uv_max_parts) >= 2:
                        bounds_dict['uvMin'] = [float(uv_min_parts[0]), float(uv_min_parts[1])]
                        bounds_dict['uvMax'] = [float(uv_max_parts[0]), float(uv_max_parts[1])]
                    assignments[uimap_id].append(bounds_dict)
    return dict(assignments)


def main():
    script_dir = Path(__file__).parent
    interactive_mode = len(sys.argv) <= 1 and not os.environ.get(DB2_DIR_ENV)

    print("=" * 60)
    print("  Master Map Coordinates Generator")
    print("=" * 60)
    print()

    # Check for command line argument
    if len(sys.argv) > 1:
        db2_dir = resolve_db2_dir(sys.argv[1])
    elif os.environ.get(DB2_DIR_ENV):
        db2_dir = resolve_db2_dir()
    else:
        # Interactive mode - prompt for folder
        default_db2 = default_db2_dir()
        if not default_db2.exists():
            default_db2 = script_dir / "db2files"
        if not default_db2.exists():
            default_db2 = None

        db2_dir = prompt_path(
            "Enter path to DB2 export files folder",
            default=default_db2,
            must_exist=True,
            is_file=False
        )

    print(f"Using DB2 folder: {db2_dir}")

    # Output file is always in the script directory
    output_file = script_dir / "master_map_alignment_coordinates.json"

    # Verify required files exist
    print()
    print("Checking required files...")

    required_files = [
        "uimap",
        "uimapxmapart",
        "uimaparttile",
        "manifestinterfacedata"
    ]
    optional_files = ["uimapassignment"]

    file_paths = {}
    for base_name in required_files:
        filepath = find_file(db2_dir, base_name)
        if filepath is None:
            print(f"  ERROR: Required file not found: {base_name}.txt or {base_name}_export.txt")
            print()
            print("Make sure you have exported the DB2 files to text format.")
            print("Use: php export_batch.php <source> <target> --keep-name")
            if interactive_mode:
                input("\nPress Enter to exit...")
            sys.exit(1)
        file_paths[base_name] = filepath
        print(f"  Found: {filepath.name}")

    for base_name in optional_files:
        filepath = find_file(db2_dir, base_name)
        if filepath:
            file_paths[base_name] = filepath
            print(f"  Found: {filepath.name}")
        else:
            print(f"  Optional file not found: {base_name}.txt")

    # Load data
    print()
    print("Loading database files...")

    uimap = parse_uimap(file_paths["uimap"])
    print(f"  Loaded {len(uimap)} maps")

    uimapxmapart = parse_uimapxmapart(file_paths["uimapxmapart"])
    print(f"  Loaded {len(uimapxmapart)} art mappings")

    uimaparttile = parse_uimaparttile(file_paths["uimaparttile"])
    print(f"  Loaded {len(uimaparttile)} tile sets")

    manifest = parse_manifest(file_paths["manifestinterfacedata"])
    print(f"  Loaded {len(manifest)} manifest entries")

    uimapassignment = {}
    if "uimapassignment" in file_paths:
        uimapassignment = parse_uimapassignment(file_paths["uimapassignment"])
        print(f"  Loaded {len(uimapassignment)} map assignments")

    # Build master data
    print()
    print("Building master coordinate data...")

    master_data = {}

    for uimap_id, map_name in uimap.items():
        entry = {
            'uiMapID': uimap_id,
            'name': map_name,
        }

        if uimap_id in uimapxmapart:
            floor_index, art_id = uimapxmapart[uimap_id]
            entry['floorIndex'] = floor_index
            entry['artID'] = art_id

            if art_id in uimaparttile:
                tiles = uimaparttile[art_id]
                layer0_tiles = [(r, c, fid) for r, c, l, fid in tiles if l == 0]

                if layer0_tiles:
                    max_row = max(t[0] for t in layer0_tiles)
                    max_col = max(t[1] for t in layer0_tiles)

                    entry['tileCount'] = len(layer0_tiles)
                    entry['gridRows'] = max_row + 1
                    entry['gridCols'] = max_col + 1
                    entry['imageWidth'] = (max_col + 1) * 256
                    entry['imageHeight'] = (max_row + 1) * 256

                    tile_files = []
                    for row, col, file_data_id in layer0_tiles:
                        if file_data_id in manifest:
                            tile_files.append({
                                'row': row,
                                'col': col,
                                'fileDataID': file_data_id,
                                'filename': manifest[file_data_id]['filename']
                            })
                    entry['tiles'] = tile_files

        if uimap_id in uimapassignment:
            assignments = uimapassignment[uimap_id]
            if len(assignments) == 1:
                entry['worldBounds'] = assignments[0]
            else:
                entry['worldBounds'] = assignments

        master_data[str(uimap_id)] = entry

    # Save JSON
    print()
    print(f"Writing {len(master_data)} map entries...")

    with open(output_file, 'w', encoding='utf-8') as f:
        json.dump(master_data, f, indent=2)

    file_size = output_file.stat().st_size / 1024 / 1024

    print()
    print("=" * 60)
    print("  Complete!")
    print("=" * 60)
    print(f"  Output: {output_file}")
    print(f"  Size: {file_size:.1f} MB")
    print()
    maps_with_tiles = sum(1 for e in master_data.values() if 'tiles' in e)
    maps_with_bounds = sum(1 for e in master_data.values() if 'worldBounds' in e)
    print(f"  Total maps: {len(master_data)}")
    print(f"  Maps with tile data: {maps_with_tiles}")
    print(f"  Maps with world bounds: {maps_with_bounds}")
    print()

    if interactive_mode:
        input("Press Enter to exit...")


if __name__ == '__main__':
    main()
