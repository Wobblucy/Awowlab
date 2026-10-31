#!/usr/bin/env python3
"""
Master Icon Pack Builder

Complete pipeline: cull icons -> package into ~5MB chunks -> output release-ready ZIPs.

Usage:
    python build_icon_pack.py                          # Use default paths
    python build_icon_pack.py --source C:/icons        # Custom source folder
    python build_icon_pack.py --output C:/release      # Custom output folder
    python build_icon_pack.py --chunk-size 8           # 8MB chunks instead of 5MB

Outputs to release folder:
    SpellIcons-001.zip, SpellIcons-002.zip, ...
    icon-chunks.json  (metadata for generate-manifest.py)
"""

import argparse
import csv
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import zipfile
from datetime import datetime, timezone
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from db2_layouts import find_export


# =============================================================================
# CONFIGURATION
# =============================================================================

SCRIPT_DIR = Path(__file__).parent
ROOT_DIR = SCRIPT_DIR.parent.parent

# Shared settings live in config.py so the season's instance list, the version and
# the export directory are stated once.
sys.path.insert(0, str(Path(__file__).resolve().parent))

from config import (
    TARGET_INSTANCES,
    RECENT_SPELL_THRESHOLD,
    VERSION,
    DB2_DIR as DB2_DIR_STR,
    WCL_CACHE_DIR as WCL_CACHE_DIR_STR,
    SPELL_DATA_BIN as SPELL_DATA_BIN_STR,
    ICON_TABLE_CSV as ICON_TABLE_CSV_STR,
    REQUIRED_ICONS_FILE as REQUIRED_ICONS_FILE_STR,
)

# Default paths
DEFAULT_SOURCE_ICONS = ROOT_DIR / 'AwowLab' / 'PNG' / 'Spell Icons'
DEFAULT_OUTPUT_DIR = ROOT_DIR / 'release'
DEFAULT_CHUNK_SIZE_MB = 5

# Spell ID sources
DB2_DIR = Path(DB2_DIR_STR)
WCL_CACHE_DIR = Path(WCL_CACHE_DIR_STR)
SPELL_DATA_BIN = Path(SPELL_DATA_BIN_STR)
ICON_TABLE_CSV = Path(ICON_TABLE_CSV_STR)
REQUIRED_ICONS_FILE = Path(REQUIRED_ICONS_FILE_STR)
PNGQUANT_EXE = ROOT_DIR / 'tools' / 'worldmap-builder' / 'pngquant.exe'

COMPRESSION_QUALITY = 50


# =============================================================================
# UTILITY FUNCTIONS
# =============================================================================

def sha256_file(filepath: Path) -> str:
    """Calculate SHA256 hash of a file."""
    sha256 = hashlib.sha256()
    with open(filepath, 'rb') as f:
        for chunk in iter(lambda: f.read(65536), b''):
            sha256.update(chunk)
    return sha256.hexdigest()


def format_size(size_bytes: int) -> str:
    """Format bytes as human-readable string."""
    if size_bytes < 1024:
        return f"{size_bytes} B"
    elif size_bytes < 1024 * 1024:
        return f"{size_bytes / 1024:.1f} KB"
    else:
        return f"{size_bytes / (1024 * 1024):.1f} MB"


# =============================================================================
# SPELL ID EXTRACTION
# =============================================================================

def extract_boss_spells():
    """Extract boss ability spell IDs from DB2 JournalEncounterSection."""
    print("\n[1/5] Extracting boss spell IDs from DB2...")

    if not DB2_DIR.exists():
        print(f"  Skipping: DB2 directory not found: {DB2_DIR}")
        return set()

    # Get encounter IDs for target instances
    encounter_file = find_export(DB2_DIR, 'journalencounter')
    encounter_ids = set()

    if encounter_file is not None:
        with open(encounter_file, 'r', encoding='utf-8') as f:
            for line in f:
                if line.startswith('Layout:'):
                    continue

                colon_idx = line.find(':')
                if colon_idx == -1:
                    continue

                try:
                    encounter_id = int(line[:colon_idx].strip())
                except ValueError:
                    continue

                bracket_start = line.find('[')
                bracket_end = line.find(']', bracket_start)

                if bracket_start == -1 or bracket_end == -1:
                    continue

                after_coords = line[bracket_end + 1:].strip()
                if after_coords.startswith(','):
                    after_coords = after_coords[1:]

                remaining_parts = after_coords.split(',')
                if len(remaining_parts) >= 2:
                    instance_id_str = remaining_parts[1].strip()
                    if instance_id_str.isdigit():
                        instance_id = int(instance_id_str)
                        if instance_id in TARGET_INSTANCES:
                            encounter_ids.add(encounter_id)

    # Extract spell IDs from sections
    section_file = find_export(DB2_DIR, 'journalencountersection')
    spell_ids = set()

    dollar_pattern = re.compile(r'\$(\d+)[sAdtoi\d]')
    hyperlink_pattern = re.compile(r'\|Hspell:(\d+)\|h')

    if section_file is not None:
        with open(section_file, 'r', encoding='utf-8') as f:
            for line in f:
                if line.startswith('Layout:'):
                    continue

                parts = line.split(',')
                line_matches = any(
                    part.strip().isdigit() and int(part.strip()) in encounter_ids
                    for part in parts
                )

                if line_matches:
                    for match in dollar_pattern.findall(line):
                        spell_ids.add(int(match))
                    for match in hyperlink_pattern.findall(line):
                        spell_ids.add(int(match))

                    if len(parts) >= 11:
                        spell_id_str = parts[10].strip()
                        if spell_id_str.isdigit():
                            spell_id = int(spell_id_str)
                            if spell_id > 1000:
                                spell_ids.add(spell_id)

    print(f"  Found {len(spell_ids)} boss spell IDs")
    return spell_ids


def extract_cache_spells():
    """Extract spell IDs from WCL cache."""
    print("\n[2/5] Extracting spell IDs from WCL cache...")

    spell_ids = set()

    if not WCL_CACHE_DIR.exists():
        print(f"  Skipping: Cache directory not found")
        return spell_ids

    page_files = list(WCL_CACHE_DIR.glob('*/fight*/page*.json'))

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
        except (json.JSONDecodeError, IOError):
            continue

    print(f"  Found {len(spell_ids)} spell IDs from {len(page_files)} cache files")
    return spell_ids


def extract_class_spells():
    """Read spell IDs from spell_data binary file."""
    print("\n[3/5] Reading class spell IDs...")

    spell_ids = set()

    if not SPELL_DATA_BIN.exists():
        print(f"  Skipping: Spell data file not found")
        return spell_ids

    with open(SPELL_DATA_BIN, 'rb') as f:
        header = f.read(4)
        if len(header) < 4:
            return spell_ids

        count = struct.unpack('<I', header)[0]

        for _ in range(count):
            record = f.read(20)
            if len(record) < 20:
                break
            spell_id = struct.unpack('<I', record[:4])[0]
            if spell_id > 0:
                spell_ids.add(spell_id)

    print(f"  Found {len(spell_ids)} class spell IDs")
    return spell_ids


def get_recent_spells(icon_mapping: dict) -> set:
    """Get spell IDs above threshold (recent additions)."""
    print("\n[4/5] Finding recent spells...")

    recent = {sid for sid in icon_mapping.keys() if sid >= RECENT_SPELL_THRESHOLD}
    print(f"  Found {len(recent)} recent spells (ID >= {RECENT_SPELL_THRESHOLD})")
    return recent


def load_required_icons() -> set:
    """Load extra required icons from required_icons.txt."""
    print("\n[5/5] Loading required icons list...")

    icons = set()

    if not REQUIRED_ICONS_FILE.exists():
        print(f"  Skipping: required_icons.txt not found")
        return icons

    with open(REQUIRED_ICONS_FILE, 'r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith('#'):
                icons.add(line.lower())

    print(f"  Found {len(icons)} required icons")
    return icons


# =============================================================================
# ICON MAPPING
# =============================================================================

def load_icon_mapping() -> dict:
    """Load spell_icon_table.csv -> {spell_id: icon_filename}."""
    mapping = {}

    if not ICON_TABLE_CSV.exists():
        print(f"Warning: Icon table not found: {ICON_TABLE_CSV}")
        return mapping

    with open(ICON_TABLE_CSV, 'r', encoding='utf-8') as f:
        reader = csv.reader(f)
        next(reader, None)  # Skip header

        for row in reader:
            if len(row) >= 4:
                try:
                    spell_id = int(row[0])
                    icon_filename = row[3].strip()

                    if icon_filename and 'NOT_FOUND' not in icon_filename.upper():
                        # Lowercase before swapping the extension. The table
                        # writes some names fully uppercase ("...NOURISH.BLP"),
                        # and a case-sensitive replace leaves those as .BLP -
                        # they then miss every file on disk and get reported
                        # as absent when the icon is sitting right there.
                        icon_filename = icon_filename.lower().replace('.blp', '.png')
                        mapping[spell_id] = icon_filename
                except (ValueError, IndexError):
                    continue

    return mapping


# =============================================================================
# CULLING
# =============================================================================

def cull_icons(spell_ids: set, icon_mapping: dict, required_icons: set,
               source_dir: Path, temp_dir: Path) -> tuple[int, int, set]:
    """Copy only required icons to temp directory."""
    print("\nCulling icons...")

    temp_dir.mkdir(parents=True, exist_ok=True)

    if not source_dir.exists():
        print(f"  Error: Source icons directory not found: {source_dir}")
        return 0, 0, set()

    # Get icons from spell IDs
    spell_icons = set()
    for spell_id in spell_ids:
        if spell_id in icon_mapping:
            spell_icons.add(icon_mapping[spell_id])

    # Combine with required icons
    all_required = spell_icons | required_icons
    print(f"  {len(spell_icons)} from spell IDs + {len(required_icons)} required = {len(all_required)} total")

    # Clear temp directory
    if temp_dir.exists():
        for f in temp_dir.glob('*.png'):
            f.unlink()

    # Copy icons
    # Missing icons are named, not just counted. This is the tool that builds
    # the pack users download, so a silent count here means shipping blank
    # squares without knowing which spells they belong to.
    copied = 0
    missing = []
    copied_files = set()

    for icon_name in sorted(all_required):
        src = source_dir / icon_name
        if src.exists():
            dst = temp_dir / icon_name
            shutil.copy2(src, dst)
            copied += 1
            copied_files.add(icon_name)
        else:
            missing.append(icon_name)

    print(f"  Copied: {copied}, Missing: {len(missing)}")
    if missing:
        print(f"  Not found in {source_dir}:")
        for name in missing:
            print(f"    {name}")
    return copied, missing, copied_files


# =============================================================================
# COMPRESSION (Optional)
# =============================================================================

def compress_icons(input_dir: Path, output_dir: Path) -> bool:
    """Compress icons with pngquant."""
    print("\nCompressing icons...")

    if not PNGQUANT_EXE.exists():
        print(f"  Skipping: pngquant not found at {PNGQUANT_EXE}")
        return False

    output_dir.mkdir(parents=True, exist_ok=True)

    png_files = list(input_dir.glob('*.png'))
    total = len(png_files)

    for i, png_file in enumerate(png_files, 1):
        output_file = output_dir / png_file.name
        cmd = [
            str(PNGQUANT_EXE),
            f'--quality=0-{COMPRESSION_QUALITY}',
            '--force',
            '--output', str(output_file),
            str(png_file)
        ]

        try:
            subprocess.run(cmd, check=True, capture_output=True)
        except subprocess.CalledProcessError:
            shutil.copy2(png_file, output_file)

        if i % 2000 == 0:
            print(f"    {i}/{total}...")

    # Calculate savings
    original = sum(f.stat().st_size for f in input_dir.glob('*.png'))
    compressed = sum(f.stat().st_size for f in output_dir.glob('*.png'))

    print(f"  Original: {format_size(original)}")
    print(f"  Compressed: {format_size(compressed)}")
    print(f"  Reduction: {(1 - compressed/original)*100:.0f}%")

    return True


# =============================================================================
# CHUNKING
# =============================================================================

def package_chunks(icons_dir: Path, output_dir: Path, chunk_size_mb: float) -> list[dict]:
    """Package icons into ~chunk_size_mb ZIP files."""
    print(f"\nPackaging into ~{chunk_size_mb}MB chunks...")

    chunk_size_bytes = int(chunk_size_mb * 1024 * 1024)

    # Get all PNG files sorted by name for consistent chunking
    png_files = sorted(icons_dir.glob('*.png'), key=lambda p: p.name.lower())

    if not png_files:
        print("  Error: No PNG files found!")
        return []

    # Group files into chunks
    chunks = []
    current_chunk = []
    current_size = 0

    for png_file in png_files:
        file_size = png_file.stat().st_size

        # Start new chunk if this would exceed limit (unless chunk is empty)
        if current_size + file_size > chunk_size_bytes and current_chunk:
            chunks.append(current_chunk)
            current_chunk = []
            current_size = 0

        current_chunk.append(png_file)
        current_size += file_size

    # Don't forget the last chunk
    if current_chunk:
        chunks.append(current_chunk)

    print(f"  Split {len(png_files)} files into {len(chunks)} chunks")

    # Remove old chunk ZIPs
    for old_zip in output_dir.glob('SpellIcons-*.zip'):
        old_zip.unlink()

    # Create ZIPs and collect metadata
    chunk_metadata = []

    for i, chunk_files in enumerate(chunks, 1):
        zip_name = f"SpellIcons-{i:03d}.zip"
        zip_path = output_dir / zip_name

        # Create ZIP
        with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zf:
            for png_file in chunk_files:
                zf.write(png_file, png_file.name)

        # Calculate hash and size
        file_hash = sha256_file(zip_path)
        file_size = zip_path.stat().st_size

        chunk_metadata.append({
            'filename': zip_name,
            'part': i,
            'sha256': file_hash,
            'size': file_size,
            'file_count': len(chunk_files)
        })

        print(f"    {zip_name}: {len(chunk_files)} files, {format_size(file_size)}")

    return chunk_metadata


# =============================================================================
# METADATA OUTPUT
# =============================================================================

def write_chunk_metadata(chunks: list[dict], output_dir: Path):
    """Write chunk metadata to both output dir and assets-hashes.json."""
    metadata = {
        'generated_at': datetime.now(timezone.utc).isoformat(),
        'extract_to': 'PNG/Spell Icons',
        'group': 'SpellIcons',
        'total_chunks': len(chunks),
        'chunks': chunks
    }

    # Write to output dir for reference
    output_file = output_dir / 'icon-chunks.json'
    with open(output_file, 'w', encoding='utf-8') as f:
        json.dump(metadata, f, indent=2)
    print(f"\nChunk metadata written to: {output_file}")

    # Also update tools/release/assets-hashes.json for generate-manifest.py
    assets_hashes_file = SCRIPT_DIR.parent / 'assets-hashes.json'
    if assets_hashes_file.exists():
        with open(assets_hashes_file, 'r', encoding='utf-8') as f:
            assets_data = json.load(f)
    else:
        assets_data = {
            'assets_tag': 'assets-v2',
            'archives': {},
            'optional_packs': {}
        }

    # Add/update chunked_archives section
    assets_data['chunked_archives'] = {
        'SpellIcons': {
            'extract_to': 'PNG/Spell Icons',
            'group': 'SpellIcons',
            'chunks': chunks
        }
    }
    assets_data['generated_at'] = datetime.now(timezone.utc).isoformat()

    with open(assets_hashes_file, 'w', encoding='utf-8') as f:
        json.dump(assets_data, f, indent=2)
    print(f"Updated assets-hashes.json with chunk metadata")

    return metadata


# =============================================================================
# MAIN
# =============================================================================

def main():
    parser = argparse.ArgumentParser(
        description='Build icon pack: cull → compress → chunk → package'
    )
    parser.add_argument('--source', type=Path, default=DEFAULT_SOURCE_ICONS,
                        help=f'Source icons directory (default: {DEFAULT_SOURCE_ICONS})')
    parser.add_argument('--output', type=Path, default=DEFAULT_OUTPUT_DIR,
                        help=f'Output directory for ZIPs (default: {DEFAULT_OUTPUT_DIR})')
    parser.add_argument('--chunk-size', type=float, default=DEFAULT_CHUNK_SIZE_MB,
                        help=f'Target chunk size in MB (default: {DEFAULT_CHUNK_SIZE_MB})')
    parser.add_argument('--no-compress', action='store_true',
                        help='Skip pngquant compression step')

    args = parser.parse_args()

    print("=" * 60)
    print("Icon Pack Builder")
    print("=" * 60)
    print(f"Source:     {args.source}")
    print(f"Output:     {args.output}")
    print(f"Chunk size: {args.chunk_size} MB")

    # Create temp directories
    temp_dir = SCRIPT_DIR / 'temp' / 'culled'
    compressed_dir = SCRIPT_DIR / 'temp' / 'compressed'

    # Step 1: Load icon mapping
    print("\nLoading icon mapping...")
    icon_mapping = load_icon_mapping()
    print(f"  Loaded {len(icon_mapping)} spell-to-icon mappings")

    # Step 2: Extract spell IDs from all sources
    boss_ids = extract_boss_spells()
    cache_ids = extract_cache_spells()
    class_ids = extract_class_spells()
    recent_ids = get_recent_spells(icon_mapping)
    required_icons = load_required_icons()

    all_spell_ids = boss_ids | cache_ids | class_ids | recent_ids

    print("\n" + "=" * 60)
    print("Spell ID Summary")
    print("=" * 60)
    print(f"  Boss abilities:     {len(boss_ids):>8}")
    print(f"  Cache abilities:    {len(cache_ids):>8}")
    print(f"  Class abilities:    {len(class_ids):>8}")
    print(f"  Recent spells:      {len(recent_ids):>8}")
    print(f"  Total unique IDs:   {len(all_spell_ids):>8}")
    print(f"  Required icons:     {len(required_icons):>8}")

    # Step 3: Cull icons
    copied, missing, _ = cull_icons(
        all_spell_ids, icon_mapping, required_icons,
        args.source, temp_dir
    )

    # Step 4: Compress (optional)
    if not args.no_compress and PNGQUANT_EXE.exists():
        compress_icons(temp_dir, compressed_dir)
        icons_to_package = compressed_dir
    else:
        icons_to_package = temp_dir

    # Step 5: Package into chunks
    args.output.mkdir(parents=True, exist_ok=True)
    chunks = package_chunks(icons_to_package, args.output, args.chunk_size)

    # Step 6: Write metadata
    metadata = write_chunk_metadata(chunks, args.output)

    # Summary
    total_size = sum(c['size'] for c in chunks)
    total_files = sum(c['file_count'] for c in chunks)

    print("\n" + "=" * 60)
    print("Final Results")
    print("=" * 60)
    print(f"  Total icons:      {total_files}")
    print(f"  Total chunks:     {len(chunks)}")
    print(f"  Total size:       {format_size(total_size)}")
    print(f"  Output directory: {args.output}")
    print("\nChunk files:")
    for chunk in chunks:
        print(f"    {chunk['filename']}: {format_size(chunk['size'])}")

    # Cleanup temp directories
    print("\nCleaning up temp files...")
    if temp_dir.exists():
        shutil.rmtree(temp_dir.parent)

    print("\nDone!")


if __name__ == '__main__':
    main()
