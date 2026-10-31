"""
Compress the exported icon PNGs with pngquant.

Usage:
    python tools/spell-icon-generator/compress_icons.py [--source <dir>] [--out <dir>]

Reads icon-pack/PNG/Spell Icons/ and writes icon-pack/PNG/Spell Icons - Compressed/
at the repository root unless told otherwise.
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

# Paths relative to this script's location
SCRIPT_DIR = Path(__file__).parent
ROOT_DIR = SCRIPT_DIR.parent.parent

INPUT_DIR = ROOT_DIR / "icon-pack" / "PNG" / "Spell Icons"
OUTPUT_DIR = ROOT_DIR / "icon-pack" / "PNG" / "Spell Icons - Compressed"
PNGQUANT = ROOT_DIR / "tools" / "worldmap-builder" / "pngquant.exe"
QUALITY = 50


def main():
    parser = argparse.ArgumentParser(description="Compress icon PNGs with pngquant.")
    parser.add_argument("--source", default=str(INPUT_DIR),
                        help="folder of exported icons (default: icon-pack/PNG/Spell Icons)")
    parser.add_argument("--out", default=str(OUTPUT_DIR),
                        help="folder for the compressed icons "
                             "(default: icon-pack/PNG/Spell Icons - Compressed)")
    parser.add_argument("--pngquant",
                        help="pngquant executable (default: tools/worldmap-builder/pngquant.exe, "
                             "else pngquant on PATH)")
    args = parser.parse_args()

    pngquant = args.pngquant or (str(PNGQUANT) if PNGQUANT.exists() else shutil.which("pngquant"))
    if not pngquant or not (Path(pngquant).exists() or shutil.which(pngquant)):
        sys.exit("pngquant not found. Put it at tools/worldmap-builder/pngquant.exe, "
                 "on PATH, or pass --pngquant.")

    input_dir, output_dir = Path(args.source), Path(args.out)
    os.makedirs(output_dir, exist_ok=True)

    png_files = list(input_dir.glob("*.png"))
    total = len(png_files)

    print(f"Found {total} PNG files to compress")
    print(f"Input:  {input_dir}")
    print(f"Output: {output_dir}")
    print()

    failed = 0
    for i, png_file in enumerate(png_files, 1):
        output_file = output_dir / png_file.name

        cmd = [
            pngquant,
            f"--quality=0-{QUALITY}",
            "--force",
            "--output", str(output_file),
            str(png_file)
        ]

        try:
            subprocess.run(cmd, check=True, capture_output=True)
        except subprocess.CalledProcessError:
            print(f"Failed: {png_file.name}")
            failed += 1
        if i % 500 == 0 or i == total:
            print(f"Processed {i}/{total}")

    print(f"\nDone! {total - failed} compressed, {failed} failed.")
    # Keep the window open when started by double-clicking compress_icons.bat.
    if sys.stdin.isatty():
        try:
            input("Press Enter to exit...")
        except EOFError:
            pass


if __name__ == "__main__":
    main()
