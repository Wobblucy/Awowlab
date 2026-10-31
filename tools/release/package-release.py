#!/usr/bin/env python3
"""
Stage the files for an AwowLab release, reusing archives that have not changed.

This script:
1. Empties the release folder
2. Copies AwowLab.exe, the updater (twice, see DIRECT_FILES) and awowlab.ico
3. Builds shaders.zip and data.zip, reusing the copy in release-cache/ when
   the source folder has not changed, and checks each holds its required files

It stops there. The manifest, version.json, signature and checksums come from
generate-manifest.py, and tools/release/release.py runs both in order, so
normally you run that instead.

Static assets (maps, spell icons, triggers, demo replays) are not staged here.
They live on a separate assets release; see docs/UPDATE-FLOW.md, Step 8.

Usage:
    python tools/release/package-release.py <version> [--build-dir DIR] [--out DIR]

Example:
    python tools/release/package-release.py 1.1.2
"""

import argparse
import hashlib
import shutil
import sys
import zipfile
from pathlib import Path

import awowlab_config


REPO_ROOT = Path(awowlab_config.REPO_ROOT)

# Defaults, all relative to the repository root
DEFAULT_BUILD_DIR = REPO_ROOT / 'out' / 'build' / 'x64-release'
DEFAULT_RELEASE_DIR = REPO_ROOT / 'release'
DEFAULT_CACHE_DIR = REPO_ROOT / 'release-cache'


def app_dir(build_dir: Path) -> Path:
    """Folder the x64-release build writes AwowLab.exe into."""
    return Path(build_dir) / 'AwowLab' / 'Release'


# Archives that ship with every versioned release.
#
# 'source' is relative to the build output folder when 'from_build' is set,
# otherwise to the repository root. 'required_files' are paths inside the
# archive that MUST be present; packaging stops if any are missing, which
# catches both an incomplete source folder and a stale cached ZIP.
ARCHIVE_SOURCES = {
    'shaders.zip': {
        'source': 'shaders',
        'from_build': True,
        'extract_to': 'shaders',
        'required_files': ['floor_vert.spv', 'floor_frag.spv',
                           'sphere_vert.spv', 'sphere_frag.spv'],
    },
    'data.zip': {
        'source': 'AwowLab/data',
        'from_build': False,
        'extract_to': 'data',
        'required_files': ['lang/en_US.csv', 'fonts/NotoSans-Regular.ttf'],
    },
}

# Files copied as they are: (source, path in the release folder).
# The updater goes in twice. The copy at the root is what a new user downloads
# to install; updater/AwowLabUpdater.exe is the one an installed app keeps up
# to date through the manifest. Both upload as AwowLabUpdater.exe, so they
# have to be the same file.
DIRECT_FILES = [
    ('build', 'AwowLab.exe', 'AwowLab.exe'),
    ('build', 'updater/AwowLabUpdater.exe', 'AwowLabUpdater.exe'),
    ('build', 'updater/AwowLabUpdater.exe', 'updater/AwowLabUpdater.exe'),
    ('repo', 'AwowLab/awowlab.ico', 'awowlab.ico'),
]


def sha256_file(filepath: Path) -> str:
    """Calculate SHA256 hash of a file."""
    sha256 = hashlib.sha256()
    with open(filepath, 'rb') as f:
        for chunk in iter(lambda: f.read(65536), b''):
            sha256.update(chunk)
    return sha256.hexdigest()


def hash_directory(directory: Path) -> str:
    """Hash the names, sizes and modification times of every file in a folder.

    Cheap change detection for the archive cache: it changes when a file is
    added, removed or touched, without reading any contents.
    """
    if not directory.exists():
        return ""

    sha256 = hashlib.sha256()
    for filepath in sorted(directory.rglob('*')):
        if filepath.is_file():
            stat = filepath.stat()
            sha256.update(str(filepath.relative_to(directory)).encode('utf-8'))
            sha256.update(str(stat.st_size).encode('utf-8'))
            sha256.update(str(int(stat.st_mtime)).encode('utf-8'))
    return sha256.hexdigest()


def create_zip(source_dir: Path, zip_path: Path) -> None:
    """Create a ZIP archive from a directory, skipping dot files."""
    print(f"  Creating {zip_path.name} from {source_dir}...")

    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zf:
        file_count = 0
        for filepath in source_dir.rglob('*'):
            if filepath.is_file() and not filepath.name.startswith('.'):
                zf.write(filepath, filepath.relative_to(source_dir))
                file_count += 1
                if file_count % 1000 == 0:
                    print(f"    {file_count} files...")
        print(f"    Done: {file_count} files")


def missing_archive_entries(zip_path: Path, required_files: list) -> list:
    """Return the required paths that are not inside the archive."""
    if not required_files:
        return []
    with zipfile.ZipFile(zip_path, 'r') as zf:
        present = {name.replace('\\', '/') for name in zf.namelist()}
    return [f for f in required_files if f not in present]


def verify_archive_contents(zip_path: Path, required_files: list) -> None:
    """Stop if the archive is missing any required entries.

    Catches the case where a cached ZIP predates a file being added to the
    source folder, so a critical file silently goes missing.
    """
    missing = missing_archive_entries(zip_path, required_files)
    if missing:
        print(f"\nERROR: {zip_path.name} is missing required file(s): {', '.join(missing)}")
        print("  Delete the cached ZIP in release-cache/ and re-run to rebuild it,")
        print("  or confirm the file exists in the source directory.")
        sys.exit(1)


def cached_zip_state(zip_name: str, source_dir: Path, cache_dir: Path) -> str:
    """'missing', 'cached' or 'rebuild' for an archive, without touching anything."""
    current_hash = hash_directory(source_dir)
    if not current_hash:
        return 'missing'
    cached_zip = cache_dir / zip_name
    hash_file = cache_dir / f"{zip_name}.hash"
    if cached_zip.exists() and hash_file.exists() and hash_file.read_text().strip() == current_hash:
        return 'cached'
    return 'rebuild'


def get_or_create_cached_zip(zip_name: str, source_dir: Path, cache_dir: Path) -> Path | None:
    """Return the cached ZIP, building it first if the source changed."""
    state = cached_zip_state(zip_name, source_dir, cache_dir)
    if state == 'missing':
        print(f"  Warning: Source directory not found: {source_dir}")
        return None

    cached_zip = cache_dir / zip_name
    if state == 'cached':
        print(f"  Using cached {zip_name} (source unchanged)")
        return cached_zip

    print(f"  Building {zip_name} (no cache, or source changed)")
    cache_dir.mkdir(parents=True, exist_ok=True)
    create_zip(source_dir, cached_zip)
    (cache_dir / f"{zip_name}.hash").write_text(hash_directory(source_dir))
    return cached_zip


def archive_source(config: dict, build_dir: Path) -> Path:
    base = app_dir(build_dir) if config['from_build'] else REPO_ROOT
    return base / config['source']


def direct_file_source(origin: str, rel: str, build_dir: Path) -> Path:
    return (app_dir(build_dir) if origin == 'build' else REPO_ROOT) / rel


def stage_release(release_dir: Path, build_dir: Path, cache_dir: Path,
                  dry_run: bool = False) -> list:
    """Empty release_dir and fill it with the executables and archives.

    Returns the staged paths relative to release_dir. With dry_run, prints
    what would be written and changes nothing. Stops (sys.exit) when a source
    file is missing or an archive lacks a required file.
    """
    release_dir = Path(release_dir)
    staged = []
    missing = []

    for origin, rel, _ in DIRECT_FILES:
        src = direct_file_source(origin, rel, build_dir)
        if not src.exists() and str(src) not in missing:
            missing.append(str(src))
    for zip_name, config in ARCHIVE_SOURCES.items():
        src = archive_source(config, build_dir)
        if not src.is_dir():
            missing.append(f"{src} (for {zip_name})")
    if missing and dry_run:
        # A dry run before a build: the build step would create these
        print("  not built yet (a real run stops if the build does not produce them):")
        for m in missing:
            print(f"    {m}")
    elif missing:
        print("ERROR: the build is incomplete; these are missing:")
        for m in missing:
            print(f"  {m}")
        print("Build the x64-release preset first (or point --build-dir at one).")
        sys.exit(1)

    if dry_run:
        if release_dir.exists():
            print(f"  would empty {release_dir}")
        for origin, rel, dst in DIRECT_FILES:
            src = direct_file_source(origin, rel, build_dir)
            print(f"  would copy {src} -> {release_dir / dst}")
            staged.append(dst)
        for zip_name, config in ARCHIVE_SOURCES.items():
            src = archive_source(config, build_dir)
            state = cached_zip_state(zip_name, src, cache_dir)
            if state == 'missing':
                print(f"  would build {zip_name} from {src} once it exists, then copy to {release_dir / zip_name}")
            elif state == 'cached':
                print(f"  would copy cached {cache_dir / zip_name} -> {release_dir / zip_name}")
            else:
                print(f"  would build {cache_dir / zip_name} from {src}, then copy to {release_dir / zip_name}")
            staged.append(zip_name)
        return staged

    if release_dir.exists():
        shutil.rmtree(release_dir)
    release_dir.mkdir(parents=True)
    print(f"Emptied release directory: {release_dir}")

    print("\nCopying files...")
    for origin, rel, dst in DIRECT_FILES:
        src = direct_file_source(origin, rel, build_dir)
        dst_path = release_dir / dst
        dst_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst_path)
        print(f"  {dst}: {src.stat().st_size / (1024 * 1024):.1f} MB")
        staged.append(dst)

    print("\nProcessing archives...")
    for zip_name, config in ARCHIVE_SOURCES.items():
        cached_zip = get_or_create_cached_zip(zip_name, archive_source(config, build_dir), cache_dir)
        verify_archive_contents(cached_zip, config.get('required_files', []))
        shutil.copy2(cached_zip, release_dir / zip_name)
        print(f"    Copied to release: {cached_zip.stat().st_size / (1024 * 1024):.1f} MB")
        staged.append(zip_name)

    return staged


def main():
    parser = argparse.ArgumentParser(description='Stage the files for an AwowLab release.')
    parser.add_argument('version', help='Version number (e.g. 1.1.2)')
    parser.add_argument('--build-dir', type=Path, default=DEFAULT_BUILD_DIR,
                        help='x64-release build folder (default: out/build/x64-release)')
    parser.add_argument('--out', type=Path, default=DEFAULT_RELEASE_DIR,
                        help='Release folder to fill (default: release)')
    parser.add_argument('--cache-dir', type=Path, default=DEFAULT_CACHE_DIR,
                        help='Where built archives are kept between runs (default: release-cache)')
    args = parser.parse_args()

    print(f"Staging AwowLab v{args.version}")
    print("=" * 50)
    stage_release(args.out, args.build_dir, args.cache_dir)

    print(f"\nStaged in: {Path(args.out).absolute()}")
    print("Next: python tools/release/generate-manifest.py "
          f"{args.out} {args.version}   (or use tools/release/release.py for the whole run)")


if __name__ == '__main__':
    main()
