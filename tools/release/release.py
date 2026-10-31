#!/usr/bin/env python3
"""
Prepare, and separately publish, an AwowLab release.

    python tools/release/release.py 1.1.2
        Checks the version against AwowLab/Core/Version.h, builds the
        x64-release preset, stages release/, builds shaders.zip and data.zip,
        writes manifest.json and version.json, signs the manifest, writes
        SHA256SUMS.txt and checks the whole folder. Nothing is uploaded.

    python tools/release/release.py 1.1.2 --publish --notes-file notes.md
        Checks the prepared folder again and uploads it with `gh release
        create` to update_repo in awowlab.config.json.

Signing is required, because the updater refuses an unsigned release. Put
the private key file in signing_key_path in awowlab.config.json (or set
AWOWLAB_SIGNING_KEY, or pass --sign-key); you are asked for its password. See tools/release/SIGNING.md, including how a fork
makes its own key.

The settings key must be the release one. awowlab.config.json has to exist
with settings_key set by hand and settings_key_generated false: a key CMake
made up for a personal build would leave every user unable to read their
settings, so the script stops instead.

Other options:
    --skip-build        use the build that is already there
    --build-dir DIR     with --skip-build, a build folder other than out/build/x64-release
    --out DIR           release folder (default: release)
    --dry-run           print every file it would write and command it would
                        run, and change nothing
    --offline           do not ask GitHub whether the static assets exist on
                        the assets tag (the only network use before --publish)
    --stage-only        stop before signing; for checking the packaging
                        without the key. The folder cannot be published.

Static assets (maps, spell icon chunks, triggers, demo replays) live on their
own release tag, recorded in tools/release/assets-hashes.json. To cut a new
one, put the complete set in a folder and add
    --assets-tag assets-vN --assets-dir DIR
when preparing (assets-hashes.json is rewritten for the new tag) and again
with --publish (the assets release is created before the app release).
"""

import argparse
import base64
import importlib.util
import json
import os
import re
import shlex
import subprocess
import sys
from pathlib import Path

import awowlab_config


TOOLS_DIR = Path(__file__).resolve().parent
REPO_ROOT = Path(awowlab_config.REPO_ROOT)
VERSION_HEADER = REPO_ROOT / 'AwowLab' / 'Core' / 'Version.h'
PRESET = 'x64-release'

VERSION_PATTERN = re.compile(r'^\d+\.\d+\.\d+$')

# Files generate-manifest writes into the release folder
GENERATED_FILES = ['manifest.json', 'manifest.json.sig', 'version.json', 'SHA256SUMS.txt']

# Staged but not uploaded under its own name: GitHub releases are flat, so
# this goes up as the root AwowLabUpdater.exe, which has to be the same file.
NOT_UPLOADED = {'updater/AwowLabUpdater.exe'}


def load_script(name):
    """Import one of the hyphen-named scripts next to this one."""
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'), TOOLS_DIR / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


package = load_script('package-release')
manifest_tool = load_script('generate-manifest')
assets_tool = load_script('generate-assets-hashes')


def fail(message):
    print(f"\nERROR: {message}")
    sys.exit(1)


def heading(text):
    print(f"\n== {text}")


def show_command(cmd):
    return ' '.join(shlex.quote(str(c)) for c in cmd)


def run(cmd, dry_run, cwd=REPO_ROOT):
    print(f"  {'would run' if dry_run else 'running'}: {show_command(cmd)}")
    if dry_run:
        return
    result = subprocess.run([str(c) for c in cmd], cwd=cwd)
    if result.returncode != 0:
        fail(f"command failed with exit code {result.returncode}: {show_command(cmd)}")


# ---------------------------------------------------------------------------
# Version


def header_versions():
    """The three version constants in Version.h."""
    text = VERSION_HEADER.read_text(encoding='utf-8')
    found = {}
    for name in ('VERSION', 'VERSION_TAG', 'VERSION_DISPLAY'):
        m = re.search(rf'\b{name}\s*=\s*"([^"]*)"', text)
        if not m:
            fail(f"{VERSION_HEADER} has no {name} constant")
        found[name] = m.group(1)
    return found


def check_version(version):
    if not VERSION_PATTERN.match(version):
        fail(f"version must look like 1.2.3 (no leading v), got {version!r}")
    found = header_versions()
    expected = {'VERSION': version, 'VERSION_TAG': f"v{version}", 'VERSION_DISPLAY': f"v{version}"}
    wrong = [f"{k} is {found[k]!r}, expected {v!r}" for k, v in expected.items() if found[k] != v]
    if wrong:
        fail(f"{VERSION_HEADER.relative_to(REPO_ROOT)} does not match {version}:\n  "
             + "\n  ".join(wrong)
             + "\nUpdate Version.h and rebuild, or pass the version it holds.")
    print(f"  Version.h agrees: {version}")


# ---------------------------------------------------------------------------
# Signing


def signing_key(args):
    return args.sign_key


def check_signing_key(args):
    key = signing_key(args)
    if not key:
        fail("No signing key is set.\n"
             "The updater refuses unsigned releases, so a release cannot be made without\n"
             "the private signing key. Put its path in signing_key_path in\n"
             "awowlab.config.json (or set AWOWLAB_SIGNING_KEY, or pass --sign-key). tools/release/SIGNING.md explains the key, and how a fork\n"
             "makes its own. To check the packaging without signing, use --stage-only.")
    if not Path(key).is_file():
        fail(f"signing key not found: {key}")
    return key


# ---------------------------------------------------------------------------
# Settings key


def built_settings_key(build_dir):
    """The settings key an existing build was configured with, or None if unknown."""
    cache = Path(build_dir) / 'CMakeCache.txt'
    if not cache.is_file():
        return None
    for line in cache.read_text(encoding='utf-8', errors='replace').splitlines():
        if line.startswith('AWOWLAB_SETTINGS_KEY:'):
            return line.split('=', 1)[1] if '=' in line else ''
    return None


def check_settings_key(args, config):
    """Stop unless the release keeps reading the settings users already have."""
    problem = awowlab_config.release_settings_key_problem(config)
    if problem:
        if args.dry_run:
            print(f"  {problem}\n  A real run would stop here.")
            return
        fail(problem)
    if args.skip_build:
        built = built_settings_key(args.build_dir)
        if built != config.settings_key:
            message = (f"{args.build_dir} was not configured with settings_key from "
                       f"{config.path}. Build again without --skip-build.")
            if args.dry_run:
                print(f"  {message}\n  A real run would stop here.")
                return
            fail(message)
    print(f"  settings_key is set in {config.path}")


def verify_signature(release_dir, public_key_hex):
    """Check manifest.json.sig against the public key the updater is built with."""
    try:
        from cryptography.exceptions import InvalidSignature
        from cryptography.hazmat.primitives import hashes
        from cryptography.hazmat.primitives.asymmetric import ec
        from cryptography.hazmat.primitives.asymmetric.utils import encode_dss_signature
    except ImportError:
        fail("checking the signature needs the 'cryptography' package: pip install cryptography")

    if not public_key_hex:
        fail("signing_public_key in awowlab.config.json is empty, so the updater could not check "
             "this signature. Put the public half of your key there (tools/release/SIGNING.md).")

    raw_key = bytes.fromhex(public_key_hex)
    public_key = ec.EllipticCurvePublicNumbers(
        int.from_bytes(raw_key[:32], 'big'), int.from_bytes(raw_key[32:], 'big'),
        ec.SECP256R1()).public_key()

    sig_path = Path(release_dir) / 'manifest.json.sig'
    raw_sig = base64.b64decode(sig_path.read_text(encoding='utf-8').strip())
    if len(raw_sig) != 64:
        return f"manifest.json.sig is {len(raw_sig)} bytes, expected 64"
    der = encode_dss_signature(int.from_bytes(raw_sig[:32], 'big'), int.from_bytes(raw_sig[32:], 'big'))
    try:
        public_key.verify(der, (Path(release_dir) / 'manifest.json').read_bytes(),
                          ec.ECDSA(hashes.SHA256()))
    except InvalidSignature:
        return ("manifest.json.sig does not match signing_public_key in awowlab.config.json; "
                "the key used is not the one this build trusts")
    return None


# ---------------------------------------------------------------------------
# Checking a staged folder


def release_files(release_dir):
    """Every file under the release folder, as forward-slash relative paths."""
    root = Path(release_dir)
    return sorted(str(p.relative_to(root)).replace('\\', '/') for p in root.rglob('*') if p.is_file())


def upload_names(manifest, version):
    """File names the manifest expects on the version tag, plus the generated files."""
    text = json.dumps(manifest)
    names = set(re.findall(rf'/download/v{re.escape(version)}/([^"]+)', text))
    return sorted(names | set(GENERATED_FILES))


def verify_release(release_dir, version, config, assets_tag, expect_signed):
    """Check the staged folder is exactly what the manifest describes. Returns problems."""
    root = Path(release_dir)
    problems = []

    manifest = json.loads((root / 'manifest.json').read_text(encoding='utf-8'))
    version_info = json.loads((root / 'version.json').read_text(encoding='utf-8'))

    if manifest.get('version') != version:
        problems.append(f"manifest.json says version {manifest.get('version')!r}")
    if manifest.get('assets_tag') != assets_tag:
        problems.append(f"manifest.json points at assets tag {manifest.get('assets_tag')!r}, not {assets_tag}")
    if version_info.get('version') != version:
        problems.append(f"version.json says version {version_info.get('version')!r}")
    expected_url = config.github_url(config.update_repo, f"releases/download/v{version}/manifest.json")
    if version_info.get('manifest_url') != expected_url:
        problems.append(f"version.json manifest_url is {version_info.get('manifest_url')!r}, "
                        f"expected {expected_url}")

    # Every file entry: staged at its install path, and the upload of the same
    # name (releases are flat) carries the same bytes.
    for entry in manifest.get('files', []):
        name = entry['url'].rsplit('/', 1)[1]
        for local in {root / entry['path'], root / name}:
            if not local.is_file():
                problems.append(f"{entry['path']} is in the manifest but {local.relative_to(root)} is not staged")
            elif local.stat().st_size != entry['size'] or package.sha256_file(local) != entry['sha256']:
                problems.append(f"{local.relative_to(root)} does not match its manifest entry")

    # Archives shipped with this version: staged, matching, holding what they must.
    required = {name: cfg.get('required_files', []) for name, cfg in package.ARCHIVE_SOURCES.items()}
    version_prefix = f"/download/v{version}/"
    for entry in manifest.get('archives', []):
        if 'chunks' in entry or version_prefix not in entry.get('url', ''):
            continue    # static asset, checked against assets-hashes.json below
        name = entry['url'].rsplit('/', 1)[1]
        local = root / name
        if not local.is_file():
            problems.append(f"{name} is in the manifest but not staged")
            continue
        if local.stat().st_size != entry['size'] or package.sha256_file(local) != entry['sha256']:
            problems.append(f"{name} does not match its manifest entry")
        for missing in package.missing_archive_entries(local, required.get(name, [])):
            problems.append(f"{name} does not contain {missing}")

    # Static entries must be the ones assets-hashes.json records for the tag.
    assets = assets_tool.load_current()
    static_hashes = {n: a['sha256'] for n, a in assets.get('archives', {}).items()}
    for group in assets.get('chunked_archives', {}).values():
        static_hashes.update({c['filename']: c['sha256'] for c in group.get('chunks', [])})
    for entry in manifest.get('archives', []):
        for part in entry.get('chunks', [entry]):
            url = part.get('url', '')
            if version_prefix in url:
                continue
            name = url.rsplit('/', 1)[1]
            if f"/download/{assets_tag}/" not in url:
                problems.append(f"{name} is fetched from {url}, not from {assets_tag}")
            elif static_hashes.get(name) != part['sha256']:
                problems.append(f"{name} hash differs from assets-hashes.json")

    # Nothing loose: every staged file is uploaded or is the in-place updater copy.
    uploads = set(upload_names(manifest, version))
    for rel in release_files(root):
        if rel in NOT_UPLOADED or rel in uploads:
            continue
        if rel == 'manifest.json.sig' and not expect_signed:
            continue
        problems.append(f"{rel} is in the release folder but not in the manifest or upload set")
    for name in uploads:
        if name == 'manifest.json.sig' and not expect_signed:
            continue
        if not (root / name).is_file():
            problems.append(f"{name} would be uploaded but is not staged")

    # Checksums file covers the folder and is current.
    sums = {}
    for line in (root / 'SHA256SUMS.txt').read_text(encoding='utf-8').splitlines():
        if line.strip():
            digest, rel = line.split('  ', 1)
            sums[rel] = digest
    for rel in release_files(root):
        if rel == 'SHA256SUMS.txt':
            continue
        if rel not in sums:
            problems.append(f"SHA256SUMS.txt does not list {rel}")
        elif sums[rel] != package.sha256_file(root / rel):
            problems.append(f"SHA256SUMS.txt has a stale hash for {rel}")

    if expect_signed:
        if not (root / 'manifest.json.sig').is_file():
            problems.append("manifest.json.sig is missing; the updater will refuse this release")
        else:
            bad = verify_signature(root, config.signing_public_key)
            if bad:
                problems.append(bad)

    return manifest, problems


# ---------------------------------------------------------------------------
# Static assets


def prepare_assets(args, dry_run):
    """Re-hash a complete assets folder into assets-hashes.json for a new tag."""
    heading(f"Static assets: {args.assets_tag} from {args.assets_dir}")
    current = assets_tool.load_current()
    if args.assets_tag == current.get('assets_tag'):
        fail(f"{args.assets_tag} is already the recorded assets tag. Cut a new tag rather than "
             "replacing files on the old one: manifests published earlier still point at it.")
    data = assets_tool.regenerate(Path(args.assets_dir), args.assets_tag, current)
    if dry_run:
        print(f"  would write {assets_tool.ASSETS_HASHES_PATH}")
        return
    with open(assets_tool.ASSETS_HASHES_PATH, 'w', encoding='utf-8') as f:
        json.dump(data, f, indent=2)
        f.write('\n')
    print(f"  Wrote {assets_tool.ASSETS_HASHES_PATH} (commit it with the release)")


def assets_upload(args):
    """Files for a new assets release, checked against assets-hashes.json."""
    data = assets_tool.load_current()
    if data.get('assets_tag') != args.assets_tag:
        fail(f"assets-hashes.json records {data.get('assets_tag')}, not {args.assets_tag}. "
             "Prepare the release with --assets-tag/--assets-dir first.")
    folder = Path(args.assets_dir)
    problems = assets_tool.check_assets_dir(folder, data)
    expected = dict((n, a['sha256']) for n, a in data.get('archives', {}).items())
    for group in data.get('chunked_archives', {}).values():
        expected.update((c['filename'], c['sha256']) for c in group.get('chunks', []))
    for pack_id, pack in data.get('optional_packs', {}).items():
        expected[pack.get('path') or f"{pack_id}.zip"] = pack['sha256']
    for name, digest in expected.items():
        path = folder / name
        if path.is_file() and package.sha256_file(path) != digest:
            problems.append(f"{name} has changed since assets-hashes.json was written")
    if problems:
        fail("the assets folder does not match assets-hashes.json:\n  " + "\n  ".join(problems))
    return [folder / name for name in assets_tool.expected_files(data)]


# ---------------------------------------------------------------------------
# The two modes


def prepare(args, config):
    dry_run = args.dry_run
    release_dir = Path(args.out)
    build_dir = Path(args.build_dir)

    heading("Version")
    check_version(args.version)

    heading("Settings key")
    check_settings_key(args, config)

    if not args.stage_only:
        heading("Signing key")
        if dry_run:
            key = signing_key(args)
            print(f"  {'would sign with ' + key if key else 'no signing key is set; a real run would stop here'}")
        else:
            print(f"  {check_signing_key(args)}")

    heading(f"Build ({PRESET})")
    if args.skip_build:
        print(f"  skipped; using {build_dir}")
        exe = package.app_dir(build_dir) / 'AwowLab.exe'
        if exe.exists() and VERSION_HEADER.stat().st_mtime > exe.stat().st_mtime:
            print(f"  Warning: Version.h is newer than {exe}. If the version changed since that "
                  "build, the executable reports the old one; rebuild.")
    else:
        run(['cmake', '--preset', PRESET], dry_run)
        run(['cmake', '--build', build_dir, '--config', 'Release', '--clean-first'], dry_run)

    heading(f"Stage {release_dir}")
    package.stage_release(release_dir, build_dir, Path(args.cache_dir), dry_run=dry_run)

    if args.assets_dir:
        prepare_assets(args, dry_run)

    heading("Manifest")
    if dry_run:
        for name in GENERATED_FILES:
            if name == 'manifest.json.sig' and args.stage_only:
                continue
            print(f"  would write {release_dir / name}")
        print(f"  static assets from {args.assets_tag} ({config.assets_repo})")
        if not args.offline and not args.assets_dir:
            print(f"  would run: gh release view {args.assets_tag} --repo {config.assets_repo} "
                  "--json assets (read-only check that every static asset is published)")
        print("\nDry run: nothing was changed.")
        return

    manifest = manifest_tool.generate_manifest(str(release_dir), args.version, args.assets_tag,
                                               config.update_repo, config.assets_repo)
    manifest_path = release_dir / 'manifest.json'
    with open(manifest_path, 'w', encoding='utf-8') as f:
        json.dump(manifest, f, indent=2)
    with open(release_dir / 'version.json', 'w', encoding='utf-8') as f:
        json.dump(manifest_tool.generate_version_json(args.version, config.update_repo), f, indent=2)
    print(f"  Wrote {manifest_path} and version.json")

    if args.stage_only:
        print("  Not signed (--stage-only). This folder cannot be published.")
    else:
        heading("Sign")
        sig = manifest_tool.sign_manifest(str(manifest_path), signing_key(args))
        print(f"  Wrote {sig}")

    manifest_tool.write_checksums(str(release_dir))
    # A new assets tag is not published yet, so there is nothing to ask GitHub
    # about; --publish checks the assets folder against assets-hashes.json instead.
    manifest_tool.verify_manifest_urls(manifest, str(release_dir), args.version,
                                       args.assets_tag, config.assets_repo,
                                       offline=args.offline or bool(args.assets_dir))

    heading("Check the staged folder")
    manifest, problems = verify_release(release_dir, args.version, config, args.assets_tag,
                                        expect_signed=not args.stage_only)
    if problems:
        fail("the release folder is not right:\n  " + "\n  ".join(problems))
    print("  Every manifest entry is staged and matches; archives hold their required files;")
    print("  SHA256SUMS.txt is current" + ("" if args.stage_only else "; the signature matches signing_public_key"))

    summary(args, config, manifest, release_dir)


def summary(args, config, manifest, release_dir):
    heading(f"AwowLab v{args.version} is ready in {release_dir.resolve()}")
    for rel in release_files(release_dir):
        size = (release_dir / rel).stat().st_size
        shown = f"{size / 1024:.1f} KB" if size < 1024 * 1024 else f"{size / (1024 * 1024):.1f} MB"
        print(f"  {rel:32} {shown}")
    static = sum(1 for a in manifest['archives'] if f"/download/v{args.version}/" not in json.dumps(a))
    print(f"  {len(manifest['files'])} files and {len(manifest['archives'])} archives in the manifest "
          f"({static} from {args.assets_tag})")
    print(f"  Signed: {'no (--stage-only)' if args.stage_only else 'yes'}")

    print("\nNext:")
    if args.stage_only:
        print("  Run again without --stage-only, with a signing key set, to make a release you can publish.")
    else:
        assets = f" --assets-tag {args.assets_tag} --assets-dir {args.assets_dir}" if args.assets_dir else ""
        print(f"  python tools/release/release.py {args.version} --publish --notes-file <notes.md>{assets}")
        print(f"    uploads to {config.update_repo}" + (f", after creating {args.assets_tag}" if assets else ""))


def publish(args, config):
    dry_run = args.dry_run
    release_dir = Path(args.out)

    heading("Version")
    check_version(args.version)
    if not args.notes_file:
        fail("--publish needs --notes-file with the release notes")
    if not Path(args.notes_file).is_file():
        fail(f"notes file not found: {args.notes_file}")

    heading(f"Check {release_dir}")
    if not (release_dir / 'manifest.json').is_file():
        fail(f"{release_dir} holds no prepared release; run release.py {args.version} first")
    manifest, problems = verify_release(release_dir, args.version, config, args.assets_tag,
                                        expect_signed=True)
    if problems:
        fail("the release folder is not right:\n  " + "\n  ".join(problems))
    print("  Folder matches its manifest and the signature is valid")

    if args.bridge_repo:
        if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", args.bridge_repo):
            fail(f"--bridge-repo must look like owner/name, got {args.bridge_repo!r}")
        if args.bridge_repo.lower() == config.update_repo.lower():
            fail("--bridge-repo is the same repository as update_repo")

    tag = f"v{args.version}"
    targets = [config.update_repo] + ([args.bridge_repo] if args.bridge_repo else [])
    for repo in targets:
        if not dry_run:
            exists = subprocess.run(['gh', 'release', 'view', tag, '--repo', repo],
                                    capture_output=True, text=True)
            if exists.returncode == 0:
                fail(f"{repo} already has a release {tag}")
        else:
            print(f"  would run: gh release view {tag} --repo {repo} (stop if it exists)")

    if args.assets_dir:
        heading(f"Assets release {args.assets_tag}")
        files = assets_upload(args)
        run(['gh', 'release', 'create', args.assets_tag, '--repo', config.assets_repo,
             '--title', f"Static assets {args.assets_tag}",
             '--notes', 'Maps, spell icons and other static files the updater installs.',
             *files], dry_run)

    heading(f"App release {tag}")
    uploads = [release_dir / name for name in upload_names(manifest, args.version)]
    run(['gh', 'release', 'create', tag, '--repo', config.update_repo,
         '--title', f"AwowLab {tag}", '--notes-file', args.notes_file, *uploads], dry_run)

    if args.bridge_repo:
        # Installs from before a move still ask the old repository for its
        # latest release. The same signed files go there too; the manifest's
        # download links already point at update_repo, so whoever updates
        # from the bridge lands on the new repository from then on.
        heading(f"Bridge release {tag} on {args.bridge_repo}")
        run(['gh', 'release', 'create', tag, '--repo', args.bridge_repo,
             '--title', f"AwowLab {tag}", '--notes-file', args.notes_file, *uploads], dry_run)

    if dry_run:
        print("\nDry run: nothing was uploaded.")
    else:
        for repo in targets:
            print(f"\nPublished {config.github_url(repo, 'releases/tag/' + tag)}")


def main():
    parser = argparse.ArgumentParser(
        description='Prepare (and with --publish, upload) an AwowLab release.',
        epilog='See the top of this file, or docs/UPDATE-FLOW.md Step 8.')
    parser.add_argument('version', help='Version to release, e.g. 1.1.2; must match Version.h')
    parser.add_argument('--publish', action='store_true',
                        help='Upload an already prepared release folder with gh')
    parser.add_argument('--notes-file', help='Release notes for --publish')
    parser.add_argument('--bridge-repo', metavar='OWNER/NAME',
                        help='With --publish, also release to this old repository, so installs '
                             'that still update from it move to update_repo')
    parser.add_argument('--skip-build', action='store_true', help='Use the existing build')
    parser.add_argument('--build-dir', default=package.DEFAULT_BUILD_DIR,
                        help='Build folder (default: out/build/x64-release); needs --skip-build')
    parser.add_argument('--out', default=package.DEFAULT_RELEASE_DIR,
                        help='Release folder (default: release)')
    parser.add_argument('--cache-dir', default=package.DEFAULT_CACHE_DIR,
                        help='Archive cache (default: release-cache)')
    parser.add_argument('--assets-tag', default=manifest_tool.current_assets_tag(),
                        help='Static assets tag the manifest points at '
                             '(default: assets_tag in assets-hashes.json)')
    parser.add_argument('--assets-dir',
                        help='Folder holding a complete new assets set for --assets-tag')
    parser.add_argument('--sign-key', default='',
                        help='Private key file (default: AWOWLAB_SIGNING_KEY, then signing_key_path in awowlab.config.json)')
    parser.add_argument('--stage-only', action='store_true',
                        help='Stop before signing; the result cannot be published')
    parser.add_argument('--offline', action='store_true',
                        help='Skip the read-only GitHub check of the static assets')
    parser.add_argument('--dry-run', action='store_true',
                        help='Print what would be written and run, and change nothing')
    args = parser.parse_args()

    if not args.skip_build and Path(args.build_dir).resolve() != Path(package.DEFAULT_BUILD_DIR).resolve():
        parser.error(f"--build-dir only works with --skip-build; the {PRESET} preset builds into "
                     "out/build/x64-release")
    if args.assets_dir and not args.assets_tag:
        parser.error("--assets-dir needs --assets-tag")
    if args.assets_dir and args.assets_tag == manifest_tool.current_assets_tag() and not args.publish:
        parser.error("--assets-dir needs a new --assets-tag, not the one already recorded")
    if args.publish and args.stage_only:
        parser.error("--stage-only cannot be published")
    if not args.assets_tag:
        parser.error("no --assets-tag given and assets-hashes.json records none")

    try:
        config = awowlab_config.load()
    except awowlab_config.ConfigError as error:
        fail(str(error))
    args.sign_key = args.sign_key or config.signing_key

    print(f"AwowLab release {args.version}{' (dry run)' if args.dry_run else ''}")
    print(f"  release repository: {config.update_repo}")
    print(f"  assets: {args.assets_tag} on {config.assets_repo}")

    if args.publish:
        publish(args, config)
    else:
        prepare(args, config)


if __name__ == '__main__':
    main()
