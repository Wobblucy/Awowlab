#!/usr/bin/env python3
"""
Compress encounter map images using pngquant.
Preserves filenames exactly for backend compatibility.

The encounter's position and scale are encoded in each filename, which is why
they are copied through untouched - see EncounterTextureManager.cpp.

Usage:
    python tools/encounter-map-compressor/compress_encounter_maps.py <input_dir>

Needs pngquant: either at tools/worldmap-builder/pngquant.exe or on PATH.
Download from https://pngquant.org/ .
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

# Paths
SCRIPT_DIR = Path(__file__).parent
ROOT_DIR = SCRIPT_DIR.parent.parent

OUTPUT_DIR = ROOT_DIR / "AwowLab" / "PNG" / "EncounterMaps"
BUNDLED_PNGQUANT = ROOT_DIR / "tools" / "worldmap-builder" / "pngquant.exe"
QUALITY = 50  # Same as spell icons


def find_pngquant():
    """Prefer the copy beside the other tools, else fall back to PATH."""
    if BUNDLED_PNGQUANT.exists():
        return str(BUNDLED_PNGQUANT)
    found = shutil.which("pngquant")
    return found


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        print(f"Output goes to: {OUTPUT_DIR}")
        return 2

    INPUT_DIR = Path(sys.argv[1].strip().strip('"'))

    pngquant = find_pngquant()
    if pngquant is None:
        print("Error: pngquant not found.")
        print(f"  Looked at: {BUNDLED_PNGQUANT}")
        print("  and on PATH. Download it from https://pngquant.org/ and place it")
        print("  at the path above, or install it so it is on PATH.")
        return 1
    PNGQUANT = Path(pngquant)

    # Verify input directory exists
    if not INPUT_DIR.exists():
        print(f"Error: Input directory not found: {INPUT_DIR}")
        return 1

    # Create output directory
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    # Get all PNG files
    png_files = list(INPUT_DIR.glob("*.png"))
    total = len(png_files)

    if total == 0:
        print(f"No PNG files found in {INPUT_DIR}")
        return 1

    print(f"Found {total} PNG files to compress")
    print(f"Input:  {INPUT_DIR}")
    print(f"Output: {OUTPUT_DIR}")
    print(f"Quality: 0-{QUALITY}")
    print()

    success_count = 0
    total_input_size = 0
    total_output_size = 0

    for i, png_file in enumerate(png_files, 1):
        output_file = OUTPUT_DIR / png_file.name  # Preserve filename exactly

        cmd = [
            str(PNGQUANT),
            f"--quality=0-{QUALITY}",
            "--force",
            "--output", str(output_file),
            str(png_file)
        ]

        try:
            subprocess.run(cmd, check=True, capture_output=True)

            input_size = png_file.stat().st_size
            output_size = output_file.stat().st_size
            total_input_size += input_size
            total_output_size += output_size

            reduction = (1 - output_size / input_size) * 100
            print(f"[{i}/{total}] {png_file.name}: {input_size / 1024 / 1024:.1f} MB -> {output_size / 1024 / 1024:.1f} MB ({reduction:.0f}% reduction)")
            success_count += 1
        except subprocess.CalledProcessError as e:
            print(f"[{i}/{total}] FAILED: {png_file.name}")
            print(f"         Error: {e.stderr.decode() if e.stderr else 'Unknown error'}")

    print()
    print(f"Compressed {success_count}/{total} files")
    print(f"Total input:  {total_input_size / 1024 / 1024:.1f} MB")
    print(f"Total output: {total_output_size / 1024 / 1024:.1f} MB")
    print(f"Overall reduction: {(1 - total_output_size / total_input_size) * 100:.0f}%")
    print(f"\nOutput directory: {OUTPUT_DIR}")

    return 0 if success_count == total else 1


if __name__ == "__main__":
    exit(main())
