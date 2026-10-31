#!/usr/bin/env python3
"""
Convert extracted BLP map tiles to the PNG tiles the stitcher reads.

stitch_map_tiles.py matches tiles by filename stem against
master_map_alignment_coordinates.json, so names carry through unchanged:
MurderRow_A1.blp -> MurderRow_A1.png.

Pillow decodes BLP directly, so no external converter is involved.

Usage:
    python convert_tiles_to_png.py [input_dir] [output_dir]

Defaults: tiles_blp -> tiles, both beside this script.
"""

import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("ERROR: Pillow not found. Install with:")
    print(f'  "{sys.executable}" -m pip install Pillow')
    sys.exit(1)


def main():
    here = Path(__file__).resolve().parent
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else here / "tiles_blp"
    dst = Path(sys.argv[2]) if len(sys.argv) > 2 else here / "tiles"

    if not src.is_dir():
        print(f"ERROR: input directory not found: {src}")
        return 1

    dst.mkdir(parents=True, exist_ok=True)

    blps = sorted(src.glob("*.blp"))
    if not blps:
        print(f"No .blp files in {src}")
        return 1

    converted = skipped = failed = 0
    for blp in blps:
        out = dst / (blp.stem + ".png")
        # Skip tiles already converted from an unchanged source.
        if out.exists() and out.stat().st_mtime >= blp.stat().st_mtime:
            skipped += 1
            continue
        try:
            with Image.open(blp) as im:
                im.convert("RGBA").save(out)
            converted += 1
        except Exception as exc:
            print(f"  FAILED {blp.name}: {exc}")
            failed += 1

    print(f"converted {converted}, up to date {skipped}, failed {failed}")
    print(f"PNG tiles in {dst}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
