#!/usr/bin/env python3
"""
Stitch map tiles into complete images using master_map_alignment_coordinates.json.
Auto-detects which maps have tiles available and stitches each floor separately.

Output naming: "{UIMapID}.png"

Double-click to run interactively.

Requirements:
  - Python 3.8+ with Pillow library (pip install Pillow)
  - master_map_alignment_coordinates.json (run generate_master_coordinates.py first)
  - pngquant for compression (optional, download from https://pngquant.org/)
"""

import sys
import json
import subprocess
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("ERROR: Pillow library not found!")
    print()
    print(f"Python being used: {sys.executable}")
    print()
    print("Install Pillow for this Python with:")
    print(f'  "{sys.executable}" -m pip install Pillow')
    print()
    print("Or run this script with the correct Python:")
    print(f'  python "{__file__}"')
    if sys.stdin.isatty():
        input("\nPress Enter to exit...")
    sys.exit(1)


TILE_SIZE = 256


def get_content_bounds(image: Image.Image, alpha_threshold: int = 128) -> tuple:
    """
    Find bounding box of opaque content (where alpha > threshold).
    Returns (left, top, right, bottom) or None if fully transparent.
    """
    if image.mode != 'RGBA':
        return (0, 0, image.width, image.height)

    # Get alpha channel and find bounding box of opaque pixels
    alpha = image.split()[3]
    bbox = alpha.point(lambda x: 255 if x > alpha_threshold else 0).getbbox()
    return bbox  # Returns (left, top, right, bottom) or None


def calculate_cropped_world_bounds(
    original_bounds: dict,
    original_size: tuple,
    crop_bbox: tuple
) -> dict:
    """
    Calculate new world bounds for cropped image region.

    Args:
        original_bounds: dict with minX, minY, maxX, maxY
        original_size: (width, height) in pixels
        crop_bbox: (left, top, right, bottom) in pixels

    Returns:
        dict with new minX, minY, maxX, maxY and imageWidth, imageHeight
    """
    orig_width, orig_height = original_size
    left, top, right, bottom = crop_bbox

    # World coordinate range
    world_width = original_bounds['maxX'] - original_bounds['minX']
    world_height = original_bounds['maxY'] - original_bounds['minY']

    # Pixel to world scale
    px_to_world_x = world_width / orig_width
    px_to_world_y = world_height / orig_height

    # Calculate new bounds
    # X maps from left to right of image
    new_minX = original_bounds['minX'] + (left * px_to_world_x)
    new_maxX = original_bounds['minX'] + (right * px_to_world_x)
    # Y is INVERTED: image Y=0 is at top, but world maxY is at top (north)
    # So cropping from image top reduces maxY, cropping from image bottom increases minY
    new_maxY = original_bounds['maxY'] - (top * px_to_world_y)
    new_minY = original_bounds['maxY'] - (bottom * px_to_world_y)

    return {
        'minX': new_minX,
        'minY': new_minY,
        'maxX': new_maxX,
        'maxY': new_maxY,
        'imageWidth': right - left,
        'imageHeight': bottom - top
    }


def write_bounds_csv(output_dir: Path, bounds_data: list):
    """Write CSV file with world bounds matching runtime format."""
    csv_path = output_dir / "map_world_bounds.csv"
    with open(csv_path, 'w') as f:
        f.write("mapId,minX,minY,maxX,maxY,imageWidth,imageHeight\n")
        for entry in bounds_data:
            # Use original (uncropped) bounds to match runtime format
            f.write(f"{entry['mapId']},{entry['minX']},{entry['minY']},"
                    f"{entry['maxX']},{entry['maxY']},"
                    f"{entry['imageWidth']},{entry['imageHeight']}\n")
    print(f"\nWrote bounds CSV: {csv_path}")


def prompt_input(prompt_text: str, default: str = None) -> str:
    """Prompt user for input with optional default."""
    if default:
        user_input = input(f"{prompt_text} [{default}]: ").strip()
        return user_input if user_input else default
    else:
        return input(f"{prompt_text}: ").strip()


def prompt_path(prompt_text: str, default: Path = None, must_exist: bool = True, is_file: bool = False) -> Path:
    """Prompt user for a path with optional default."""
    while True:
        if default:
            user_input = input(f"{prompt_text} [{default}]: ").strip().strip('"\'')
            path = Path(user_input) if user_input else default
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


def index_available_tiles(png_dir: Path) -> dict:
    """
    Index all PNG files in directory by lowercase filename (without extension).
    Returns: {filename_stem_lower: Path}
    """
    tiles = {}
    for file in png_dir.iterdir():
        if file.is_file() and file.suffix.lower() == '.png':
            key = file.stem.lower()
            tiles[key] = file
    return tiles


def find_matching_maps(master_data: dict, available_tiles: dict) -> list:
    """
    Find all maps in master_data that have matching tiles available.
    Returns list of (uimap_id, map_entry, matched_tiles) tuples.
    """
    matches = []

    for uimap_id, entry in master_data.items():
        if 'tiles' not in entry or not entry['tiles']:
            continue

        # Check how many tiles match
        matched = {}
        for tile_info in entry['tiles']:
            filename = tile_info.get('filename', '')
            if filename.lower().endswith('.blp'):
                filename = filename[:-4]

            filename_lower = filename.lower()
            if filename_lower in available_tiles:
                matched[(tile_info['row'], tile_info['col'])] = available_tiles[filename_lower]

        if matched:
            matches.append((uimap_id, entry, matched))

    return matches


def stitch_tiles(tile_positions: dict) -> Image.Image:
    """Stitch tiles into a single image based on row/col positions."""
    if not tile_positions:
        return None

    max_row = max(pos[0] for pos in tile_positions.keys())
    max_col = max(pos[1] for pos in tile_positions.keys())

    width = (max_col + 1) * TILE_SIZE
    height = (max_row + 1) * TILE_SIZE

    result = Image.new('RGBA', (width, height), (0, 0, 0, 0))

    for (row, col), tile_path in tile_positions.items():
        try:
            tile = Image.open(tile_path)
            if tile.mode != 'RGBA':
                tile = tile.convert('RGBA')
            x = col * TILE_SIZE
            y = row * TILE_SIZE
            result.paste(tile, (x, y))
            tile.close()
        except Exception as e:
            print(f"    Warning: Could not load tile {tile_path.name}: {e}")

    return result


def compress_with_pngquant(input_path: Path, output_path: Path,
                           quality: str, pngquant_exe: str) -> bool:
    """Compress PNG using pngquant."""
    try:
        cmd = [
            pngquant_exe,
            "--force",
            "--quality", quality,
            "--output", str(output_path),
            str(input_path)
        ]
        result = subprocess.run(cmd, capture_output=True, text=True)

        if result.returncode == 0:
            return True
        elif result.returncode == 99:
            print(f"    Note: pngquant quality target not met, using original")
            import shutil
            shutil.copy2(input_path, output_path)
            return True
        else:
            print(f"    Warning: pngquant failed: {result.stderr}")
            return False

    except FileNotFoundError:
        print(f"    Warning: pngquant not found at '{pngquant_exe}'")
        return False
    except Exception as e:
        print(f"    Warning: pngquant error: {e}")
        return False


def sanitize_filename(name: str) -> str:
    """Remove/replace characters invalid in Windows filenames."""
    invalid_chars = '<>:"/\\|?*'
    for char in invalid_chars:
        name = name.replace(char, '_')
    return name.strip()


def main():
    script_dir = Path(__file__).parent

    print("=" * 60)
    print("  Map Tile Stitcher")
    print("=" * 60)
    print()

    # Step 1: Load master JSON
    master_json = script_dir / "master_map_alignment_coordinates.json"
    if not master_json.exists():
        print("ERROR: master_map_alignment_coordinates.json not found!")
        print()
        print("Please run generate_master_coordinates.py first to create it.")
        if sys.stdin.isatty():
            input("\nPress Enter to exit...")
        sys.exit(1)

    print("Loading master coordinates...")
    with open(master_json, 'r', encoding='utf-8') as f:
        master_data = json.load(f)
    print(f"  Loaded {len(master_data)} map definitions")

    # Step 2: Get PNG tiles folder
    print()
    default_png = script_dir / "tiles"
    if not default_png.exists():
        default_png = None

    png_dir = prompt_path(
        "Enter path to PNG tiles folder",
        default=default_png,
        must_exist=True,
        is_file=False
    )

    # Index available tiles
    print()
    print("Scanning for tiles...")
    available_tiles = index_available_tiles(png_dir)
    print(f"  Found {len(available_tiles)} PNG files")

    # Find maps with matching tiles
    print()
    print("Matching tiles to maps...")
    matches = find_matching_maps(master_data, available_tiles)

    if not matches:
        print("  ERROR: No matching maps found!")
        print()
        print("  Make sure your PNG filenames match those in the master JSON.")
        print("  Example: ManaforgeRaid_A1.png, Durotar1.png, etc.")
        if sys.stdin.isatty():
            input("\nPress Enter to exit...")
        sys.exit(1)

    # Show what we found
    print(f"  Found {len(matches)} map(s) with matching tiles:")
    for uimap_id, entry, matched in matches:
        total_tiles = len(entry.get('tiles', []))
        print(f"    - {entry['name']} (ID: {uimap_id}): {len(matched)}/{total_tiles} tiles")

    # Step 3: Output folder
    print()
    default_output = script_dir / "output"
    output_dir = prompt_path(
        "Enter output folder",
        default=default_output,
        must_exist=False,
        is_file=False
    )
    output_dir.mkdir(parents=True, exist_ok=True)

    # Step 4: pngquant path (optional)
    print()
    default_pngquant = script_dir / "pngquant" / "pngquant.exe"
    if not default_pngquant.exists():
        import shutil
        pngquant_in_path = shutil.which("pngquant")
        if pngquant_in_path:
            default_pngquant = Path(pngquant_in_path)
        else:
            default_pngquant = None

    use_compression = True
    if default_pngquant and default_pngquant.exists():
        pngquant_exe = default_pngquant
        print(f"Using pngquant: {pngquant_exe}")
    else:
        print("pngquant not found. Download from: https://pngquant.org/")
        skip = prompt_input("Skip compression? (y/n)", default="y")
        if skip.lower() in ('y', 'yes'):
            use_compression = False
            pngquant_exe = None
        else:
            pngquant_exe = prompt_path(
                "Enter path to pngquant.exe",
                must_exist=True,
                is_file=True
            )

    # Compression quality
    quality = "65-80"
    if use_compression:
        quality = prompt_input("Enter compression quality (min-max)", default="65-80")

    # Process each map
    print()
    print("=" * 60)
    print("  Processing...")
    print("=" * 60)

    # Collect bounds data for CSV output
    all_bounds_data = []

    for uimap_id, entry, tile_positions in matches:
        map_name = entry.get('name', f'Unknown_{uimap_id}')
        print()
        print(f"Stitching: {map_name} (ID: {uimap_id})...")

        result_image = stitch_tiles(tile_positions)

        if result_image is None:
            print(f"  ERROR: Failed to stitch!")
            continue

        original_size = (result_image.width, result_image.height)
        print(f"  Created image: {result_image.width}x{result_image.height}")

        # Detect and crop to content bounds (remove transparent edges)
        content_bbox = get_content_bounds(result_image)
        if content_bbox is None:
            print(f"  WARNING: Image is fully transparent, skipping!")
            result_image.close()
            continue

        # Check if cropping is needed
        left, top, right, bottom = content_bbox
        if left > 0 or top > 0 or right < result_image.width or bottom < result_image.height:
            print(f"  Cropping transparent edges: ({left}, {top}) to ({right}, {bottom})")
            cropped_image = result_image.crop(content_bbox)
            result_image.close()
            result_image = cropped_image
            print(f"  Cropped size: {result_image.width}x{result_image.height}")
        else:
            print(f"  No transparent edges to crop")

        # Use original world bounds from master JSON (matches runtime format)
        # worldBounds can be a dict (single floor) or list (multiple floors)
        original_bounds = entry.get('worldBounds', {})
        if isinstance(original_bounds, list) and original_bounds:
            # Use first floor bounds (orderIndex 0)
            original_bounds = original_bounds[0]

        img_w = entry.get('imageWidth', result_image.width)
        img_h = entry.get('imageHeight', result_image.height)

        if original_bounds and isinstance(original_bounds, dict):
            all_bounds_data.append({
                'mapId': uimap_id,
                'minX': original_bounds.get('minX', 0),
                'minY': original_bounds.get('minY', 0),
                'maxX': original_bounds.get('maxX', 0),
                'maxY': original_bounds.get('maxY', 0),
                'imageWidth': img_w,
                'imageHeight': img_h
            })
            print(f"  World bounds: ({original_bounds.get('minX', 0):.2f}, {original_bounds.get('minY', 0):.2f}) "
                  f"to ({original_bounds.get('maxX', 0):.2f}, {original_bounds.get('maxY', 0):.2f})")
        else:
            print(f"  WARNING: No worldBounds in master data, using image dimensions only")
            all_bounds_data.append({
                'mapId': uimap_id,
                'minX': 0, 'minY': 0,
                'maxX': img_w, 'maxY': img_h,
                'imageWidth': img_w,
                'imageHeight': img_h
            })

        # Generate output filename: "{UIMapID}.png"
        output_filename = f"{uimap_id}.png"
        output_path = output_dir / output_filename

        if use_compression and pngquant_exe:
            # Save temp file
            temp_path = output_path.with_suffix('.temp.png')
            result_image.save(temp_path, 'PNG')
            result_image.close()

            original_size = temp_path.stat().st_size
            print(f"  Uncompressed: {original_size / 1024:.1f} KB")

            # Compress
            print(f"  Compressing...")
            if compress_with_pngquant(temp_path, output_path, quality, str(pngquant_exe)):
                compressed_size = output_path.stat().st_size
                ratio = (1 - compressed_size / original_size) * 100
                print(f"  Compressed: {compressed_size / 1024:.1f} KB ({ratio:.1f}% reduction)")
                temp_path.unlink()
            else:
                print("  Compression failed, using uncompressed version")
                temp_path.rename(output_path)
        else:
            # Save without compression
            result_image.save(output_path, 'PNG')
            result_image.close()
            file_size = output_path.stat().st_size
            print(f"  Saved: {file_size / 1024:.1f} KB")

        print(f"  Output: {output_path.name}")

    # Write bounds CSV
    if all_bounds_data:
        write_bounds_csv(output_dir, all_bounds_data)

    print()
    print("=" * 60)
    print(f"  Complete! Generated {len(matches)} map(s)")
    print("=" * 60)
    print()

    if sys.stdin.isatty():
        input("Press Enter to exit...")


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
        if sys.stdin.isatty():
            input("Press Enter to exit...")
