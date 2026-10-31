"""Download the static assets a source build does not include.

Spell icons, the full world map set and the encounter maps are too large for
the repository; official installs get them from the assets release through the
updater. A source build has no updater, so this fetches the same archives,
checks each against the SHA-256 recorded in tools/release/assets-hashes.json, and
unpacks them where the app looks.

Every build's PNG folder links back to AwowLab/PNG in the source tree, so one
download serves Debug and Release alike. The default triggers and demo replays
live next to a particular executable instead; pass --app-dir to install them.

Usage:
  python tools/fetch-assets.py
  python tools/fetch-assets.py --app-dir out/build/x64-debug/AwowLab/Debug
  python tools/fetch-assets.py --repo someone/fork-releases   # another repository

The default repository is assets_repo in awowlab.config.json.

Standard library only, so it runs before anything else is installed.
"""
import argparse
import hashlib
import json
import os
import shutil
import sys
import tempfile
import urllib.request
import zipfile

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.normpath(os.path.join(TOOLS_DIR, ".."))
SOURCE_APP_DIR = os.path.join(REPO_ROOT, "AwowLab")
HASHES_PATH = os.path.join(TOOLS_DIR, "release", "assets-hashes.json")

sys.path.insert(0, os.path.join(TOOLS_DIR, "release"))
import awowlab_config  # noqa: E402

DEFAULT_REPO = awowlab_config.load().assets_repo

# Archive -> folder it unpacks into, relative to the app folder. Mirrors
# STATIC_ARCHIVES in release/generate-manifest.py, which is what the updater follows.
PNG_ARCHIVES = {
    "Worldmaps.zip": "PNG/Worldmaps/defaults",
    "EncounterMaps.zip": "PNG/EncounterMaps/defaults",
}
APP_ARCHIVES = {
    "Triggers.zip": "triggers/defaults",
    "DemoReplays.zip": "Demo Replays",
}


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1 << 16), b""):
            digest.update(block)
    return digest.hexdigest()


def download(url, target):
    request = urllib.request.Request(url, headers={"User-Agent": "awowlab-fetch-assets"})
    with urllib.request.urlopen(request, timeout=120) as response, open(target, "wb") as out:
        shutil.copyfileobj(response, out)


def fetch_and_unpack(repo, tag, name, expected, destination, work_dir):
    """Download one archive, verify it, and unpack it over destination."""
    url = f"https://github.com/{repo}/releases/download/{tag}/{name}"
    archive = os.path.join(work_dir, name)
    print(f"  {name:22} {expected['size'] / (1024 * 1024):6.1f} MB  ", end="", flush=True)
    download(url, archive)

    actual = sha256_of(archive)
    if actual != expected["sha256"]:
        print("CHECKSUM MISMATCH")
        raise SystemExit(f"{name}: expected {expected['sha256']}, got {actual}. "
                         "Nothing was unpacked from it.")

    # Replace rather than merge, so a file dropped from the archive does not
    # linger from an older download.
    if os.path.isdir(destination):
        shutil.rmtree(destination)
    os.makedirs(destination, exist_ok=True)
    with zipfile.ZipFile(archive) as bundle:
        bundle.extractall(destination)
        count = len([n for n in bundle.namelist() if not n.endswith("/")])
    os.remove(archive)
    inside_repo = os.path.commonpath([REPO_ROOT, os.path.abspath(destination)]) == REPO_ROOT
    shown = os.path.relpath(destination, REPO_ROOT) if inside_repo else "staging"
    print(f"ok, {count} files -> {shown}")


def main():
    parser = argparse.ArgumentParser(description="Download the static assets for a source build")
    parser.add_argument("--repo", default=DEFAULT_REPO,
                        help=f"GitHub repository holding the assets release (default {DEFAULT_REPO})")
    parser.add_argument("--app-dir",
                        help="folder holding a built AwowLab.exe; also installs the default "
                             "triggers and demo replays there")
    parser.add_argument("--skip-icons", action="store_true", help="leave spell icons out")
    parser.add_argument("--force", action="store_true",
                        help="replace an existing spell icon folder")
    args = parser.parse_args()
    if awowlab_config.is_placeholder_repo(args.repo):
        parser.error(f"{args.repo} is the example's placeholder. Set assets_repo in "
                     "awowlab.config.json, or pass --repo owner/name (the published assets "
                     "are in Wobblucy/Awowlab).")

    with open(HASHES_PATH, encoding="utf-8") as handle:
        hashes = json.load(handle)
    tag = hashes["assets_tag"]
    archives = hashes["archives"]
    print(f"assets release {tag} from {args.repo}")

    with tempfile.TemporaryDirectory(prefix="awowlab-assets-") as work_dir:
        for name, folder in PNG_ARCHIVES.items():
            fetch_and_unpack(args.repo, tag, name, archives[name],
                             os.path.join(SOURCE_APP_DIR, *folder.split("/")), work_dir)

        icons = hashes["chunked_archives"]["SpellIcons"]
        icon_dir = os.path.join(SOURCE_APP_DIR, *icons["extract_to"].split("/"))
        if not args.skip_icons and os.path.isdir(icon_dir) and os.listdir(icon_dir) and not args.force:
            # Unlike the defaults folders, this one may hold icons built locally
            # with the icon pipeline, so it is not replaced without asking.
            print(f"  spell icons kept: {os.path.relpath(icon_dir, REPO_ROOT)} already has files "
                  "(pass --force to replace them)")
        elif not args.skip_icons:
            if os.path.isdir(icon_dir):
                shutil.rmtree(icon_dir)
            # Chunks share one folder, so unpack each into a staging folder and
            # merge, rather than letting each one replace the last.
            staging = os.path.join(work_dir, "icons")
            for chunk in sorted(icons["chunks"], key=lambda c: c["part"]):
                fetch_and_unpack(args.repo, tag, chunk["filename"], chunk,
                                 os.path.join(staging, str(chunk["part"])), work_dir)
            os.makedirs(icon_dir, exist_ok=True)
            for part in sorted(os.listdir(staging)):
                shutil.copytree(os.path.join(staging, part), icon_dir, dirs_exist_ok=True)
            print(f"  spell icons merged into {os.path.relpath(icon_dir, REPO_ROOT)}")

        if args.app_dir:
            if not os.path.isfile(os.path.join(args.app_dir, "AwowLab.exe")):
                raise SystemExit(f"no AwowLab.exe in {args.app_dir}; build first or fix the path")
            for name, folder in APP_ARCHIVES.items():
                fetch_and_unpack(args.repo, tag, name, archives[name],
                                 os.path.join(args.app_dir, *folder.split("/")), work_dir)
        else:
            print("  default triggers and demo replays skipped (pass --app-dir to install them)")

    print("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
