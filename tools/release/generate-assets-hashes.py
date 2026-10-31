#!/usr/bin/env python3
"""
Refresh tools/release/assets-hashes.json from a folder holding an assets release.

assets-hashes.json records the SHA256 and size of every file on the static
assets release (maps, spell icon chunks, triggers, demo replays, optional
language packs). generate-manifest.py copies those into each manifest, so the
ZIPs themselves are not needed when an app release is made.

An assets tag is a complete set, not a delta: the updater fetches every
archive from whichever tag the manifest names. So this script re-hashes every
archive, chunk and optional pack the current assets-hashes.json lists, and
refuses to write anything if one of them is missing from the folder, rather
than writing a file that leaves it out. Everything else in the file
(extract_to, display names, chunk part numbers) is kept as it is.

To add a new archive, add its entry to assets-hashes.json by hand first (and
to STATIC_ARCHIVES in generate-manifest.py), then run this.

Usage:
    python tools/release/generate-assets-hashes.py <assets_dir> <assets_tag> [--dry-run]

Example:
    python tools/release/generate-assets-hashes.py release-assets assets-v6
"""

import argparse
import copy
import hashlib
import json
import sys
import zipfile
from datetime import datetime, timezone
from pathlib import Path


ASSETS_HASHES_PATH = Path(__file__).parent / 'assets-hashes.json'

# Paths that must be inside an archive for it to be worth publishing.
# Worldmaps.zip is the only source of map_world_bounds.csv; without it every
# map renders as an empty grid floor. Mirrors ARCHIVE_MUST_CONTAIN in
# generate-manifest.py.
ARCHIVE_MUST_CONTAIN = {
    'Worldmaps.zip': ['map_world_bounds.csv'],
}


def sha256_file(filepath: Path) -> str:
    """Calculate SHA256 hash of a file."""
    sha256 = hashlib.sha256()
    with open(filepath, 'rb') as f:
        for chunk in iter(lambda: f.read(65536), b''):
            sha256.update(chunk)
    return sha256.hexdigest()


def load_current(path: Path = ASSETS_HASHES_PATH) -> dict:
    with open(path, 'r', encoding='utf-8') as f:
        return json.load(f)


def expected_files(assets_data: dict) -> list:
    """Every file name the assets release has to carry, in a stable order."""
    names = list(assets_data.get('archives', {}).keys())
    for group in assets_data.get('chunked_archives', {}).values():
        names.extend(chunk['filename'] for chunk in group.get('chunks', []))
    for pack_id, pack in assets_data.get('optional_packs', {}).items():
        names.append(pack.get('path') or f"{pack_id}.zip")
    return names


def check_assets_dir(assets_dir: Path, assets_data: dict) -> list:
    """Return a list of problems with the folder; empty means it is complete."""
    problems = []
    for name in expected_files(assets_data):
        if not (assets_dir / name).is_file():
            problems.append(f"{name} is missing from {assets_dir}")
    for name, required in ARCHIVE_MUST_CONTAIN.items():
        path = assets_dir / name
        if not path.is_file():
            continue
        with zipfile.ZipFile(path) as zf:
            present = set(zf.namelist())
        for entry in required:
            if entry not in present:
                problems.append(f"{name} does not contain {entry}")
    return problems


def regenerate(assets_dir: Path, assets_tag: str, assets_data: dict) -> dict:
    """Return a copy of assets_data with every hash and size taken from assets_dir."""
    problems = check_assets_dir(assets_dir, assets_data)
    if problems:
        print("ERROR: the assets folder is not a complete set:")
        for p in problems:
            print(f"  {p}")
        print("Nothing was written. Every archive in assets-hashes.json has to be in the folder.")
        sys.exit(1)

    data = copy.deepcopy(assets_data)
    data['assets_tag'] = assets_tag
    data['generated_at'] = datetime.now(timezone.utc).isoformat()

    for name, entry in data.get('archives', {}).items():
        path = assets_dir / name
        entry['sha256'] = sha256_file(path)
        entry['size'] = path.stat().st_size
        print(f"  {name}: {entry['size'] / (1024 * 1024):.1f} MB")

    for group_name, group in data.get('chunked_archives', {}).items():
        for chunk in group.get('chunks', []):
            path = assets_dir / chunk['filename']
            chunk['sha256'] = sha256_file(path)
            chunk['size'] = path.stat().st_size
            with zipfile.ZipFile(path) as zf:
                chunk['file_count'] = sum(1 for i in zf.infolist() if not i.is_dir())
            print(f"  {chunk['filename']}: {chunk['size'] / (1024 * 1024):.1f} MB, "
                  f"{chunk['file_count']} files ({group_name} part {chunk['part']})")

    for pack_id, pack in data.get('optional_packs', {}).items():
        path = assets_dir / (pack.get('path') or f"{pack_id}.zip")
        pack['sha256'] = sha256_file(path)
        pack['size'] = path.stat().st_size
        print(f"  {path.name}: {pack['size'] / (1024 * 1024):.1f} MB (optional pack {pack_id})")

    return data


def total_size(data: dict) -> int:
    size = sum(a['size'] for a in data.get('archives', {}).values())
    for group in data.get('chunked_archives', {}).values():
        size += sum(c['size'] for c in group.get('chunks', []))
    size += sum(p['size'] for p in data.get('optional_packs', {}).values())
    return size


def main():
    parser = argparse.ArgumentParser(
        description='Refresh assets-hashes.json from a folder holding a full assets release.')
    parser.add_argument('assets_dir', type=Path, help='Folder holding every assets archive')
    parser.add_argument('assets_tag', help='Tag the assets release is published under (e.g. assets-v6)')
    parser.add_argument('--dry-run', action='store_true',
                        help='Check and hash the folder but do not write assets-hashes.json')
    args = parser.parse_args()

    if not args.assets_dir.is_dir():
        print(f"Error: assets directory not found: {args.assets_dir}")
        sys.exit(1)

    print(f"Hashing assets for {args.assets_tag} from {args.assets_dir}")
    data = regenerate(args.assets_dir, args.assets_tag, load_current())

    if args.dry_run:
        print(f"\nWould write: {ASSETS_HASHES_PATH}")
    else:
        with open(ASSETS_HASHES_PATH, 'w', encoding='utf-8') as f:
            json.dump(data, f, indent=2)
            f.write('\n')
        print(f"\nGenerated: {ASSETS_HASHES_PATH}")
    print(f"Assets tag: {args.assets_tag}")
    print(f"Files: {len(expected_files(data))}, {total_size(data) / (1024 * 1024):.1f} MB")


if __name__ == '__main__':
    main()
