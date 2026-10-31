#!/usr/bin/env python3
"""
Extract world bounds from master_map_alignment_coordinates.json to a simple CSV.
Output: map_world_bounds.csv with columns: mapId,minX,minY,maxX,maxY,imageWidth,imageHeight

Writes directly to the canonical source location the release packages from
(AwowLab/PNG/Worldmaps/map_world_bounds.csv) so there is no separate copy step.
If this file is missing at runtime the world-bounds table is empty and every
map renders as a blank grid floor, so it must always reach that location.
"""

import json
from pathlib import Path


def main():
    script_dir = Path(__file__).parent
    json_path = script_dir / "master_map_alignment_coordinates.json"
    # repo_root/tools/worldmap-builder -> repo_root
    repo_root = script_dir.parent.parent
    output_path = repo_root / "AwowLab" / "PNG" / "Worldmaps" / "map_world_bounds.csv"

    if not json_path.exists():
        print(f"ERROR: {json_path} not found")
        return

    print(f"Loading {json_path}...")
    with open(json_path, 'r', encoding='utf-8') as f:
        data = json.load(f)

    # Ensure output directory exists
    output_path.parent.mkdir(parents=True, exist_ok=True)

    count = 0
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write("mapId,minX,minY,maxX,maxY,imageWidth,imageHeight\n")

        for map_id, entry in data.items():
            if 'worldBounds' in entry:
                bounds = entry['worldBounds']
                img_w = entry.get('imageWidth', 0)
                img_h = entry.get('imageHeight', 0)
                # Handle both dict and list formats
                if isinstance(bounds, dict):
                    min_x = bounds.get('minX', 0)
                    min_y = bounds.get('minY', 0)
                    max_x = bounds.get('maxX', 0)
                    max_y = bounds.get('maxY', 0)
                    f.write(f"{map_id},{min_x},{min_y},{max_x},{max_y},{img_w},{img_h}\n")
                    count += 1
                elif isinstance(bounds, list) and len(bounds) > 0:
                    # Use first bounds entry
                    b = bounds[0]
                    if isinstance(b, dict):
                        min_x = b.get('minX', 0)
                        min_y = b.get('minY', 0)
                        max_x = b.get('maxX', 0)
                        max_y = b.get('maxY', 0)
                        f.write(f"{map_id},{min_x},{min_y},{max_x},{max_y},{img_w},{img_h}\n")
                        count += 1

    print(f"Wrote {count} map bounds to {output_path}")

if __name__ == '__main__':
    main()
