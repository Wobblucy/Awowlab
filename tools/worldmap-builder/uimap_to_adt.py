#!/usr/bin/env python3
"""
UIMap to ADT Tile Coordinate Mapper

Maps UIMap IDs (PNG worldmap files) to WoW ADT terrain tile coordinates
for extraction with WoWExport.

Usage:
    python uimap_to_adt.py 2529           # Single map
    python uimap_to_adt.py 2529 2530 2531 # Multiple maps
    python uimap_to_adt.py                # Interactive mode
    python uimap_to_adt.py list           # List all available maps

Double-click to run interactively.
"""

import sys
import json
from pathlib import Path

# WoW ADT tile constants
ADT_SIZE = 533.33333333  # 1600/3 yards per tile
WORLD_OFFSET = 17066.66666667  # 32 * ADT_SIZE (world center offset)


def world_to_adt_tile(worldX: float, worldY: float) -> tuple:
    """Convert world coordinates to ADT tile indices."""
    tileX = int((WORLD_OFFSET - worldX) / ADT_SIZE)
    tileY = int((WORLD_OFFSET - worldY) / ADT_SIZE)
    return (tileX, tileY)


def get_adt_tiles_for_bounds(minX: float, minY: float, maxX: float, maxY: float) -> list:
    """
    Get all ADT tiles that overlap the given world coordinate bounds.

    Note: Higher world coords = lower tile indices (inverted relationship)
    """
    # Get corner tiles (swap min/max due to coordinate inversion)
    tile_at_max = world_to_adt_tile(maxX, maxY)  # max world coords = min tile
    tile_at_min = world_to_adt_tile(minX, minY)  # min world coords = max tile

    min_tileX = min(tile_at_max[0], tile_at_min[0])
    max_tileX = max(tile_at_max[0], tile_at_min[0])
    min_tileY = min(tile_at_max[1], tile_at_min[1])
    max_tileY = max(tile_at_max[1], tile_at_min[1])

    # Generate all tiles in range
    tiles = []
    for x in range(min_tileX, max_tileX + 1):
        for y in range(min_tileY, max_tileY + 1):
            tiles.append((x, y))

    return tiles


def load_master_data(script_dir: Path) -> dict:
    """Load the master map alignment coordinates JSON."""
    master_json = script_dir / "master_map_alignment_coordinates.json"

    if not master_json.exists():
        print("ERROR: master_map_alignment_coordinates.json not found!")
        print(f"Expected at: {master_json}")
        return None

    print("Loading master coordinates...")
    with open(master_json, 'r', encoding='utf-8') as f:
        return json.load(f)


def get_world_bounds(entry: dict) -> dict:
    """Extract worldBounds from a map entry, handling both dict and list formats."""
    bounds = entry.get('worldBounds', {})

    if isinstance(bounds, list) and bounds:
        # Multi-floor map: use first floor (orderIndex 0)
        bounds = bounds[0]

    if not isinstance(bounds, dict):
        return None

    return bounds


def process_uimap(uimap_id: str, master_data: dict) -> bool:
    """Process a single UIMap ID and print its ADT tile coverage."""
    if uimap_id not in master_data:
        print(f"ERROR: UIMap ID {uimap_id} not found in master data")
        return False

    entry = master_data[uimap_id]
    name = entry.get('name', 'Unknown')
    bounds = get_world_bounds(entry)

    if not bounds:
        print(f"ERROR: No worldBounds found for UIMap {uimap_id}")
        return False

    minX = bounds.get('minX', 0)
    minY = bounds.get('minY', 0)
    maxX = bounds.get('maxX', 0)
    maxY = bounds.get('maxY', 0)
    mapID = bounds.get('mapID', 'Unknown')

    # Calculate ADT tiles
    tiles = get_adt_tiles_for_bounds(minX, minY, maxX, maxY)

    # Print results
    print()
    print("=" * 60)
    print(f"  UIMap {uimap_id}: {name}")
    print("=" * 60)
    print(f"Map.db2 ID: {mapID}")
    print(f"World Bounds: ({minX:.2f}, {minY:.2f}) to ({maxX:.2f}, {maxY:.2f})")
    print()

    # Calculate grid dimensions
    if tiles:
        xs = sorted(set(t[0] for t in tiles))
        ys = sorted(set(t[1] for t in tiles))

        print(f"ADT Tiles ({len(tiles)} total):")
        print(f"  X range: {min(xs)} to {max(xs)} ({len(xs)} tiles)")
        print(f"  Y range: {min(ys)} to {max(ys)} ({len(ys)} tiles)")
        print()

        # Print grid layout
        print("Tile Grid:")
        for x in xs:
            row_tiles = [f"{x},{y}" for y in ys]
            print(f"  {' '.join(f'{t:>7}' for t in row_tiles)}")
        print()

        # Print as simple list for easy copy-paste
        print("Tile List (for WoWExport):")
        tile_strs = [f"{t[0]},{t[1]}" for t in sorted(tiles)]
        # Print in rows of 8
        for i in range(0, len(tile_strs), 8):
            print(f"  {' '.join(tile_strs[i:i+8])}")
    else:
        print("No ADT tiles found (invalid bounds?)")

    return True


def list_available_maps(master_data: dict):
    """List all available maps in the master data."""
    print()
    print("=" * 60)
    print("  Available UIMap IDs")
    print("=" * 60)
    print()

    # Group by name prefix for cleaner output
    entries = []
    for uimap_id, entry in master_data.items():
        name = entry.get('name', 'Unknown')
        bounds = get_world_bounds(entry)
        has_bounds = bounds is not None and 'minX' in bounds
        entries.append((int(uimap_id), name, has_bounds))

    # Sort by ID
    entries.sort(key=lambda x: x[0])

    # Print in columns
    for uid, name, has_bounds in entries:
        status = "" if has_bounds else " [no bounds]"
        print(f"  {uid:>5}: {name}{status}")

    print()
    print(f"Total: {len(entries)} maps")


def interactive_mode(master_data: dict):
    """Run in interactive mode, prompting for UIMap IDs."""
    print()
    print("=" * 60)
    print("  UIMap to ADT Tile Mapper - Interactive Mode")
    print("=" * 60)
    print()
    print("Enter a UIMap ID to get its ADT tile coordinates.")
    print("Commands:")
    print("  list  - Show all available UIMap IDs")
    print("  quit  - Exit the program")
    print()

    while True:
        try:
            user_input = input("UIMap ID: ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            break

        if not user_input:
            continue

        if user_input.lower() == 'quit':
            break

        if user_input.lower() == 'list':
            list_available_maps(master_data)
            continue

        # Try to process as UIMap ID
        process_uimap(user_input, master_data)


def main():
    script_dir = Path(__file__).parent

    # Load master data
    master_data = load_master_data(script_dir)
    if master_data is None:
        input("\nPress Enter to exit...")
        sys.exit(1)

    print(f"Loaded {len(master_data)} map definitions")

    # Parse command line arguments
    args = sys.argv[1:]

    if not args:
        # Interactive mode
        interactive_mode(master_data)
    elif args[0].lower() == 'list':
        # List mode
        list_available_maps(master_data)
    else:
        # Process specified UIMap IDs
        for uimap_id in args:
            process_uimap(uimap_id, master_data)

    # If running interactively (double-clicked), wait for input
    if not args and sys.stdin.isatty():
        input("\nPress Enter to exit...")


if __name__ == '__main__':
    try:
        main()
    except Exception as e:
        print()
        print("=" * 60)
        print(f"  ERROR: {e}")
        print("=" * 60)
        import traceback
        traceback.print_exc()
        print()
        input("Press Enter to exit...")
