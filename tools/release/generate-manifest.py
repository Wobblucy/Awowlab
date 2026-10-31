#!/usr/bin/env python3
"""
Generate manifest.json and version.json for AwowLab releases.
Run from project root after building release package.

The release directory should contain:
- Individual files (AwowLab.exe, awowlab.ico, etc.)
- ZIP archives for bundled content (shaders.zip, data.zip)

Static assets (SpellIcons.zip, Worldmaps.zip) are hosted in a separate
assets release. Their hashes are read from tools/release/assets-hashes.json.

The release repository and the one holding the assets release are
update_repo and assets_repo in awowlab.config.json.

Usage:
    python tools/release/generate-manifest.py release 0.17.0
    python tools/release/generate-manifest.py release 0.17.0 --assets-tag assets-v5
    python tools/release/generate-manifest.py release 0.17.0 --offline
    python tools/release/generate-manifest.py release 0.17.0 --sign-key C:\\path\\to\\awowlab-signing-key.pem

With --sign-key, manifest.json is signed (ECDSA P-256) and manifest.json.sig
is written next to it. Create the key pair with tools/release/generate-signing-key.py.
A SHA256SUMS.txt covering all release files is always written so users can
verify downloads by hand.

The assets tag defaults to the one recorded in tools/release/assets-hashes.json,
which is the only place the current tag is kept. --offline skips the read-only
check that every static asset is published on that tag.

tools/release/release.py runs this as one step of a full release.
"""

import argparse
import base64
import getpass
import json
import hashlib
import os
import sys
import re
import subprocess
from pathlib import Path
from datetime import datetime, timezone

import awowlab_config


def sha256_file(filepath: Path) -> str:
    """Calculate SHA256 hash of a file."""
    sha256 = hashlib.sha256()
    with open(filepath, 'rb') as f:
        for chunk in iter(lambda: f.read(65536), b''):
            sha256.update(chunk)
    return sha256.hexdigest()


# Static archives - hosted in separate assets release
# Key = ZIP filename, Value = extraction target directory
# Note: SpellIcons is now handled as chunked archive via icon-chunks.json
# assets-v3+: Worldmaps/EncounterMaps extract to defaults/ subfolders for user override support
STATIC_ARCHIVES = {
    'Worldmaps.zip': 'PNG/Worldmaps/defaults',
    'EncounterMaps.zip': 'PNG/EncounterMaps/defaults',
    'Triggers.zip': 'triggers/defaults',
    'DemoReplays.zip': 'Demo Replays',
}

# Legacy single-file SpellIcons (used if icon-chunks.json not present)
LEGACY_SPELL_ICONS = {
    'SpellIcons.zip': 'PNG/Spell Icons',
}

# Dynamic archives - hosted in versioned release
# Key = ZIP filename, Value = extraction target directory
DYNAMIC_ARCHIVES = {
    'shaders.zip': 'shaders',
    'data.zip': 'data',
}

# Folders to create during installation (empty directories for user content)
REQUIRED_FOLDERS = [
    'custom_models',
    'triggers',
    'triggers/defaults',              # Shipped default triggers
    'PNG/Worldmaps',                  # User overrides (defaults/ populated by Worldmaps.zip)
    'PNG/EncounterMaps',              # User overrides (defaults/ populated by EncounterMaps.zip)
]

# Minimum updater version required for this release
# Bump this when the updater gains new functionality that releases depend on
# Version history:
#   1 = Initial updater with chunked archive support
#   2 = Added defaults/ migration logic for assets-v3
UPDATER_VERSION = 2


ASSETS_HASHES_PATH = Path(__file__).parent / 'assets-hashes.json'


def current_assets_tag() -> str:
    """The assets tag recorded in assets-hashes.json, or '' if there is none."""
    try:
        with open(ASSETS_HASHES_PATH, 'r', encoding='utf-8') as f:
            return json.load(f).get('assets_tag', '')
    except (OSError, json.JSONDecodeError):
        return ''


def load_assets_hashes(assets_tag: str) -> dict:
    """Load assets-hashes.json for static asset hashes and optional packs.

    Returns:
        Full assets data dict (archives, optional_packs, chunked_archives)
    """
    assets_path = ASSETS_HASHES_PATH

    if not assets_path.exists():
        print(f"Warning: assets-hashes.json not found at {assets_path}")
        print("Static assets will not be included in manifest.")
        return {'archives': {}, 'optional_packs': {}, 'chunked_archives': {}}

    with open(assets_path, 'r', encoding='utf-8') as f:
        assets_data = json.load(f)

    # Verify assets tag matches
    if assets_data.get('assets_tag') != assets_tag:
        print(f"Warning: assets-hashes.json has tag '{assets_data.get('assets_tag')}' but '{assets_tag}' was requested.")
        print("Proceeding with hashes from file.")

    return assets_data


def load_icon_chunks_from_assets(assets_data: dict) -> dict | None:
    """Load chunked SpellIcons info from assets-hashes.json.

    Returns:
        Chunk metadata dict or None if not found/using legacy single ZIP
    """
    chunked = assets_data.get('chunked_archives', {})
    return chunked.get('SpellIcons')


def generate_manifest(release_dir: str, version: str, assets_tag: str, repo: str, assets_repo: str) -> dict:
    """Generate manifest.json for the release directory."""
    release_path = Path(release_dir)

    if not release_path.exists():
        print(f"Error: Release directory not found: {release_dir}")
        sys.exit(1)

    # GitHub release URLs
    version_url = f"https://github.com/{repo}/releases/download/v{version}"
    assets_url = f"https://github.com/{assets_repo}/releases/download/{assets_tag}"

    files = []
    archives = []
    optional_packs = {}

    # Files to exclude from manifest (they're generated or not needed)
    exclude_patterns = [
        'manifest.json',     # Also matches manifest.json.sig
        'version.json',
        'SHA256SUMS.txt',
        'icon-chunks.json',  # Metadata consumed by this script
        '.gitkeep',
        '.DS_Store',
        'Thumbs.db',
        '.archive_hash',
        '.archive_hashes',
    ]

    # Files to exclude by exact relative path
    # Root AwowLabUpdater.exe is the standalone installer - not tracked for updates
    # The maintained updater lives at updater/AwowLabUpdater.exe
    exclude_exact_paths = {
        'AwowLabUpdater.exe',
    }

    # Load static asset hashes, optional packs, and chunked archives
    assets_data = load_assets_hashes(assets_tag)
    static_hashes = assets_data.get('archives', {})
    optional_packs_config = assets_data.get('optional_packs', {})

    # Combine all archive filenames for exclusion from files list
    all_archive_filenames = set(STATIC_ARCHIVES.keys()) | set(DYNAMIC_ARCHIVES.keys()) | set(LEGACY_SPELL_ICONS.keys())

    # Check for chunked SpellIcons and add those filenames to exclusion
    icon_chunks = load_icon_chunks_from_assets(assets_data)
    if icon_chunks:
        for chunk in icon_chunks.get('chunks', []):
            all_archive_filenames.add(chunk['filename'])

    # Build optional packs for manifest
    for pack_id, pack_info in optional_packs_config.items():
        optional_packs[pack_id] = {
            'id': pack_id,
            'sha256': pack_info['sha256'],
            'size': pack_info['size'],
            'path': pack_info.get('path', ''),
            'display_name': pack_info.get('display_name', pack_id),
            'url': f"{assets_url}/{pack_id}.zip"
        }
        # Add archive extraction info if it's a ZIP
        if pack_info.get('is_archive', False):
            optional_packs[pack_id]['is_archive'] = True
            optional_packs[pack_id]['extract_to'] = pack_info.get('extract_to', '.')
        display = pack_info.get('display_name', pack_id)
        try:
            print(f"  Optional pack: {pack_id} ({display})")
        except UnicodeEncodeError:
            print(f"  Optional pack: {pack_id}")

    # Files an archive must contain to be worth shipping. Worldmaps.zip is the
    # only source of map_world_bounds.csv, and without it the app has no way to
    # position any map - every dungeon renders as an empty grid floor. That
    # shipped broken once because the archive was built from *.png and nothing
    # noticed the csv was gone, so it is checked here rather than trusted.
    ARCHIVE_MUST_CONTAIN = {
        'Worldmaps.zip': ['map_world_bounds.csv'],
    }

    for filename, required in ARCHIVE_MUST_CONTAIN.items():
        local = release_path / filename
        if not local.exists():
            continue   # Not staged this run; its hash comes from the tag
        import zipfile
        names = set(zipfile.ZipFile(local).namelist())
        missing = [r for r in required if r not in names]
        if missing:
            print(f"  ERROR: {filename} is missing {', '.join(missing)}")
            print(f"         Rebuild it with those files included before publishing.")
            sys.exit(1)
        print(f"  Verified: {filename} contains {', '.join(required)}")

    # Add static archives from assets-hashes.json
    for filename, extract_to in STATIC_ARCHIVES.items():
        if filename in static_hashes:
            hash_info = static_hashes[filename]
            archive_entry = {
                'extract_to': extract_to,
                'sha256': hash_info['sha256'],
                'size': hash_info['size'],
                'url': f"{assets_url}/{filename}"
            }
            archives.append(archive_entry)
            print(f"  Static archive: {filename} -> {extract_to} (from assets-hashes.json)")
        else:
            print(f"  Warning: {filename} not found in assets-hashes.json, skipping")

    # Handle SpellIcons - check for chunked format in assets-hashes.json
    if icon_chunks:
        # Chunked SpellIcons (new format)
        chunk_entries = []
        for chunk in icon_chunks.get('chunks', []):
            chunk_entries.append({
                'part': chunk['part'],
                'sha256': chunk['sha256'],
                'size': chunk['size'],
                'url': f"{assets_url}/{chunk['filename']}"
            })

        chunked_archive = {
            'extract_to': icon_chunks.get('extract_to', 'PNG/Spell Icons'),
            'group': icon_chunks.get('group', 'SpellIcons'),
            'chunks': chunk_entries
        }
        archives.append(chunked_archive)

        total_size = sum(c['size'] for c in chunk_entries)
        print(f"  Chunked archive: SpellIcons -> {chunked_archive['extract_to']}")
        print(f"    {len(chunk_entries)} chunks, {total_size / (1024*1024):.1f} MB total")
    else:
        # Legacy single SpellIcons.zip
        for filename, extract_to in LEGACY_SPELL_ICONS.items():
            if filename in static_hashes:
                hash_info = static_hashes[filename]
                archive_entry = {
                    'extract_to': extract_to,
                    'sha256': hash_info['sha256'],
                    'size': hash_info['size'],
                    'url': f"{assets_url}/{filename}"
                }
                archives.append(archive_entry)
                print(f"  Legacy archive: {filename} -> {extract_to} (from assets-hashes.json)")
            else:
                print(f"  Warning: {filename} not found in assets-hashes.json, skipping")

    # Process files in release directory
    for filepath in release_path.rglob('*'):
        if filepath.is_file():
            # Get relative path with forward slashes
            rel_path = filepath.relative_to(release_path)
            rel_path_str = str(rel_path).replace(chr(92), chr(47))
            filename = filepath.name

            # Skip excluded files by pattern
            if any(pattern in rel_path_str for pattern in exclude_patterns):
                continue

            # Skip excluded files by exact path
            if rel_path_str in exclude_exact_paths:
                continue

            # Skip all archive files from file list (handled separately)
            if filename in all_archive_filenames:
                # Check if it's a dynamic archive
                if filename in DYNAMIC_ARCHIVES:
                    extract_to = DYNAMIC_ARCHIVES[filename]
                    file_hash = sha256_file(filepath)
                    file_size = filepath.stat().st_size

                    archive_entry = {
                        'extract_to': extract_to,
                        'sha256': file_hash,
                        'size': file_size,
                        'url': f"{version_url}/{filename}"
                    }
                    archives.append(archive_entry)
                    print(f"  Dynamic archive: {filename} -> {extract_to}")
                # Static archives are handled above from assets-hashes.json
                continue

            # Regular file
            file_hash = sha256_file(filepath)
            file_size = filepath.stat().st_size

            # GitHub Releases flatten all files - use just filename for URL
            # but keep full relative path for local installation
            file_entry = {
                'path': rel_path_str,
                'sha256': file_hash,
                'size': file_size,
                'url': f"{version_url}/{filename}"
            }

            files.append(file_entry)

    # Sort for consistent output
    files.sort(key=lambda x: x['path'])
    archives.sort(key=lambda x: x['extract_to'])

    manifest = {
        'version': version,
        'updater_version': UPDATER_VERSION,  # Minimum updater version required
        'generated_at': datetime.now(timezone.utc).isoformat(),
        'assets_tag': assets_tag,  # Track which assets release is used
        'files': files,
        'archives': archives,
        'folders': REQUIRED_FOLDERS,
        'optional_packs': optional_packs
    }

    return manifest


def sign_manifest(manifest_path: str, key_path: str) -> str:
    """Sign manifest.json with the release key.

    Writes manifest.json.sig next to the manifest: base64 of the raw
    64-byte (r || s) ECDSA P-256 signature over the manifest file bytes.
    This is the exact format the updater's SignatureVerifier expects.

    Returns the signature file path.
    """
    try:
        from cryptography.hazmat.primitives.asymmetric import ec
        from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature
        from cryptography.hazmat.primitives import hashes, serialization
    except ImportError:
        print("Error: signing requires the 'cryptography' package: pip install cryptography")
        sys.exit(1)

    key_bytes = Path(key_path).read_bytes()
    password = getpass.getpass(f"Password for {key_path}: ").encode() or None
    private_key = serialization.load_pem_private_key(key_bytes, password=password)

    manifest_bytes = Path(manifest_path).read_bytes()
    der_signature = private_key.sign(manifest_bytes, ec.ECDSA(hashes.SHA256()))

    # BCrypt on the updater side wants raw r || s, not DER
    r, s = decode_dss_signature(der_signature)
    raw_signature = r.to_bytes(32, "big") + s.to_bytes(32, "big")

    sig_path = manifest_path + ".sig"
    Path(sig_path).write_text(base64.b64encode(raw_signature).decode(), encoding="utf-8")
    return sig_path


def write_checksums(release_dir: str) -> str:
    """Write SHA256SUMS.txt covering every file in the release directory.

    Standard sha256sum format so users can verify downloads independently.
    """
    release_path = Path(release_dir)
    lines = []
    for filepath in sorted(release_path.rglob('*')):
        if not filepath.is_file():
            continue
        if filepath.name in ('SHA256SUMS.txt', '.gitkeep', '.DS_Store', 'Thumbs.db'):
            continue
        rel = str(filepath.relative_to(release_path)).replace(chr(92), chr(47))
        lines.append(f"{sha256_file(filepath)}  {rel}")

    sums_path = os.path.join(release_dir, 'SHA256SUMS.txt')
    with open(sums_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write("\n".join(lines) + "\n")
    return sums_path


def generate_version_json(version: str, repo: str) -> dict:
    """Generate version.json for GitHub release."""
    tag = f"v{version}"
    return {
        'version': version,
        'release_date': datetime.now(timezone.utc).isoformat(),
        'manifest_url': f"https://github.com/{repo}/releases/download/{tag}/manifest.json",
        'changelog_url': f"https://github.com/{repo}/releases/tag/{tag}"
    }


def verify_manifest_urls(manifest: dict, release_dir: str, version: str,
                         assets_tag: str, assets_repo: str, offline: bool = False) -> None:
    """Check every download the manifest promises can actually be served.

    A manifest is a list of URLs an updater will fetch one by one, and a single
    bad one strands the install partway through. Two ways they go wrong:

      - a file that will ship with THIS release is named but is not staged, so
        it never gets uploaded. That happened when a working copy of
        map_world_bounds.csv was left in the release folder: the generator saw a
        loose file, wrote it into the manifest, and users got a 404.
      - a static asset is referenced on a tag that does not carry it. The Korean
        language pack sat on assets-v3 while three later manifests pointed at
        the newest tag.

    The first is checked against the staging folder; the second needs the GitHub
    CLI and is skipped with a warning when it is unavailable, since a missing
    tool should not block a build. offline skips it on purpose.
    """
    print()
    text = json.dumps(manifest)
    release_path = Path(release_dir)
    problems = []

    version_tag = f"v{version}"
    ships_now = set(re.findall(
        rf'/download/{re.escape(version_tag)}/([^"]+)', text))
    for name in sorted(ships_now):
        if not (release_path / name).exists():
            problems.append(
                f"{version_tag}/{name} is in the manifest but not staged in "
                f"{release_dir} - it will 404")

    # Anything the manifest promises from this release has to be one of the
    # files the release actually uploads. A working copy left in the staging
    # folder looks like a shippable file to the scan above, gets a URL, and
    # then never makes it into the upload list - which is exactly how
    # map_world_bounds.csv came to 404 for everyone who updated.
    uploadable = {'AwowLab.exe', 'awowlab.ico', 'AwowLabUpdater.exe',
                  'updater/AwowLabUpdater.exe', 'shaders.zip', 'data.zip',
                  'manifest.json', 'version.json', 'SHA256SUMS.txt',
                  'manifest.json.sig'}
    for name in sorted(ships_now):
        if name.replace('\\', '/') not in uploadable:
            problems.append(
                f"{version_tag}/{name} is not part of the release upload. "
                f"If it belongs to users, put it inside an archive; if it is "
                f"a working copy, take it out of {release_dir}")

    static = set(re.findall(
        rf'/download/{re.escape(assets_tag)}/([^"]+)', text))
    if static and offline:
        print(f"  Note: offline; not checking {len(static)} asset(s) on {assets_tag}")
    elif static:
        try:
            published = subprocess.run(
                ['gh', 'release', 'view', assets_tag,
                 '--repo', assets_repo,
                 '--json', 'assets', '-q', '.assets[].name'],
                capture_output=True, text=True, timeout=120)
            if published.returncode != 0:
                print(f"  Note: could not read {assets_tag} "
                      f"({published.stderr.strip()[:80]}); skipping asset check")
            else:
                have = set(published.stdout.split())
                for name in sorted(static):
                    if name not in have:
                        problems.append(
                            f"{assets_tag}/{name} is referenced but not "
                            f"published on that tag - it will 404")
        except (FileNotFoundError, subprocess.TimeoutExpired):
            print(f"  Note: gh unavailable; skipping the {assets_tag} check")

    if problems:
        print("  Manifest references files that cannot be downloaded:")
        for p in problems:
            print(f"    {p}")
        sys.exit(1)

    if offline:
        print(f"  Verified: {len(ships_now)} file(s) staged for {version_tag}")
    else:
        print(f"  Verified: {len(ships_now)} file(s) staged for {version_tag}, "
              f"{len(static)} asset(s) present on {assets_tag}")


def main():
    parser = argparse.ArgumentParser(
        description='Generate manifest.json and version.json for AwowLab releases.'
    )
    parser.add_argument('release_dir', help='Directory containing release files')
    parser.add_argument('version', help='Version number (e.g., 0.17.0)')
    parser.add_argument('--assets-tag', default=current_assets_tag(),
                        help='Assets release tag for static files '
                             '(default: assets_tag in assets-hashes.json)')
    parser.add_argument('--sign-key', default='',
                        help='Path to the release signing key PEM (default: AWOWLAB_SIGNING_KEY, '
                             'then signing_key_path in awowlab.config.json). Without one the '
                             'manifest is not signed.')
    parser.add_argument('--offline', action='store_true',
                        help='Do not ask GitHub whether the static assets are published')

    args = parser.parse_args()
    if not args.assets_tag:
        print("Error: no --assets-tag given and assets-hashes.json records none")
        sys.exit(1)

    try:
        config = awowlab_config.load()
    except awowlab_config.ConfigError as error:
        print(f"Error: {error}")
        sys.exit(1)
    args.sign_key = args.sign_key or config.signing_key
    repo = config.update_repo
    assets_repo = config.assets_repo

    print(f"Generating manifest for version {args.version}...")
    print(f"Release directory: {args.release_dir}")
    print(f"Release repository: {repo}")
    print(f"Assets tag: {args.assets_tag} ({assets_repo})")
    print()

    # Generate manifest
    manifest = generate_manifest(args.release_dir, args.version, args.assets_tag, repo, assets_repo)

    # Write manifest.json
    manifest_path = os.path.join(args.release_dir, 'manifest.json')
    with open(manifest_path, 'w', encoding='utf-8') as f:
        json.dump(manifest, f, indent=2)
    print(f"\nGenerated: {manifest_path}")

    # Generate version.json
    version_info = generate_version_json(args.version, repo)

    version_path = os.path.join(args.release_dir, 'version.json')
    with open(version_path, 'w', encoding='utf-8') as f:
        json.dump(version_info, f, indent=2)
    print(f"Generated: {version_path}")

    # Sign the manifest so updaters can verify it came from us
    if args.sign_key:
        if not os.path.exists(args.sign_key):
            print(f"Error: signing key not found: {args.sign_key}")
            sys.exit(1)
        sig_path = sign_manifest(manifest_path, args.sign_key)
        print(f"Generated: {sig_path}")
        print("Remember to upload manifest.json.sig with the release assets.")
    else:
        print("Note: manifest not signed (no --sign-key, AWOWLAB_SIGNING_KEY or signing_key_path).")

    # Published checksums so users can verify downloads by hand
    sums_path = write_checksums(args.release_dir)
    print(f"Generated: {sums_path}")

    verify_manifest_urls(manifest, args.release_dir, args.version,
                         args.assets_tag, assets_repo, offline=args.offline)

    # Print summary
    def is_static_archive(a):
        if 'chunks' in a:
            return True  # Chunked archives are always static
        return 'url' in a and 'assets-v' in a['url']

    def get_archive_size(a):
        if 'chunks' in a:
            return sum(c['size'] for c in a['chunks'])
        return a.get('size', 0)

    static_count = len([a for a in manifest['archives'] if is_static_archive(a)])
    dynamic_count = len(manifest['archives']) - static_count

    print(f"\nManifest contains:")
    print(f"  {len(manifest['files'])} files")
    print(f"  {static_count} static archives (from {args.assets_tag})")
    print(f"  {dynamic_count} dynamic archives")
    print(f"  {len(manifest['folders'])} folders")

    # Calculate sizes
    files_size = sum(f['size'] for f in manifest['files'])
    static_size = sum(get_archive_size(a) for a in manifest['archives'] if is_static_archive(a))
    dynamic_size = sum(get_archive_size(a) for a in manifest['archives'] if not is_static_archive(a))

    print(f"\nSize breakdown:")
    print(f"  Files: {files_size / (1024 * 1024):.1f} MB")
    print(f"  Static archives: {static_size / (1024 * 1024):.1f} MB (in {args.assets_tag})")
    print(f"  Dynamic archives: {dynamic_size / (1024 * 1024):.1f} MB (in v{args.version})")
    print(f"  Upload size (this release): {(files_size + dynamic_size) / (1024 * 1024):.1f} MB")

    # List files
    if manifest['files']:
        print("\nFiles:")
        for f in manifest['files']:
            size_str = f"{f['size'] / 1024:.1f} KB" if f['size'] < 1024 * 1024 else f"{f['size'] / (1024 * 1024):.1f} MB"
            print(f"  {f['path']}: {size_str}")

    # List archives
    if manifest['archives']:
        print("\nArchives:")
        for a in manifest['archives']:
            size = get_archive_size(a)
            size_str = f"{size / (1024 * 1024):.1f} MB"
            if 'chunks' in a:
                print(f"  {a['extract_to']}: {size_str} ({len(a['chunks'])} chunks, {args.assets_tag})")
            else:
                source = args.assets_tag if is_static_archive(a) else f"v{args.version}"
                print(f"  {a['extract_to']}: {size_str} ({source})")

    # List folders
    if manifest['folders']:
        print("\nFolders to create:")
        for folder in manifest['folders']:
            print(f"  {folder}/")


if __name__ == '__main__':
    main()
