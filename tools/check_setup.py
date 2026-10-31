#!/usr/bin/env python3
"""
Report what the data pipeline needs on this machine and what is missing.

Run this first, before installing anything else:

    python tools/check_setup.py

It goes through each pipeline step in docs/UPDATE-FLOW.md and lists the
programs, Python packages, settings and folders that step uses, marking each
one ok, MISSING, CHANGED (a DB2 table changed shape), warning or optional. Nothing is
changed, nothing is downloaded, and it always exits 0. Standard library only,
so it runs before tools/requirements.txt is installed.

Settings it reads (all environment variables):
    AWOWLAB_DB2_DIR      DB2 text export folder (default: rawdb2/<version>)
    AWOWLAB_WOW_DIR      WoW install root (default: C:/Program Files (x86)/World of Warcraft)
    AWOWLAB_LISTFILE     listfile for world map tile extraction
    RUSTYDEMON_CLI       rustydemon-cli executable
    WOWEXPORT_SRC        wow.export source checkout (default: wowexport-src beside this repo)
    WOWEXPORT_MMAP_NODE  mmap.node from an installed wow.export
    AWOWLAB_SIGNING_KEY  manifest signing key (release packaging only)
"""

import glob
import importlib.metadata
import importlib.util
import json
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
REPO_ROOT = TOOLS_DIR.parent
sys.path.insert(0, str(TOOLS_DIR))

import db2_layouts  # noqa: E402  (standard library only)

MIN_PYTHON = (3, 9)
DEFAULT_WOW_DIR = "C:/Program Files (x86)/World of Warcraft"

# requirements.txt name -> the module the tools import
PACKAGES = {
    "Pillow": "PIL",
    "cryptography": "cryptography",
    "deepl": "deepl",
}

OK, MISSING, CHANGED, OPTIONAL, INFO = "ok", "MISSING", "CHANGED", "optional", "-"
WARNING = "warning"   # works, but not for everything; never blocks a step
BLOCKING = (MISSING, CHANGED)


class Item:
    def __init__(self, name, status, detail=""):
        self.name, self.status, self.detail = name, status, detail


def found(name, path, missing_hint, optional=False):
    if path:
        return Item(name, OK, str(path))
    return Item(name, OPTIONAL if optional else MISSING, missing_hint)


# --- individual checks -------------------------------------------------------

def check_python():
    version = ".".join(str(n) for n in sys.version_info[:3])
    if sys.version_info[:2] >= MIN_PYTHON:
        return Item("Python", OK, f"{version} ({sys.executable})")
    want = ".".join(str(n) for n in MIN_PYTHON)
    return Item("Python", MISSING, f"{version} is too old, need {want} or later")


def check_package(dist_name, optional=False):
    module = PACKAGES[dist_name]
    try:
        spec = importlib.util.find_spec(module)
    except (ImportError, ValueError):
        spec = None
    if spec is None:
        return Item(dist_name, OPTIONAL if optional else MISSING,
                    "pip install -r tools/requirements.txt")
    try:
        version = importlib.metadata.version(dist_name)
    except importlib.metadata.PackageNotFoundError:
        version = "installed"
    return Item(dist_name, OK, version)


def run_version(cmd):
    """First line of a program's version output, or "" if it will not run."""
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=20)
    except (OSError, subprocess.SubprocessError):
        return ""
    text = (out.stdout or out.stderr).strip()
    return text.splitlines()[0] if text else ""


def check_program(name, exe, hint, version_args=("--version",), optional=False):
    path = shutil.which(exe)
    if not path:
        return Item(name, OPTIONAL if optional else MISSING, hint)
    version = run_version([path, *version_args])
    return Item(name, OK, f"{version}  ({path})" if version else path)


def find_visual_studio():
    """Install folder of Visual Studio 2022 with the C++ tools, via vswhere."""
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) \
        / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.is_file():
        return None
    try:
        out = subprocess.run(
            [str(vswhere), "-version", "[17.0,18.0)", "-products", "*",
             "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
             "-property", "installationPath"],
            capture_output=True, text=True, timeout=30).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return None
    return out.splitlines()[0] if out else None


def check_vcpkg():
    root, _ = setting("VCPKG_ROOT")
    if root is None:
        return Item("vcpkg", MISSING, "VCPKG_ROOT not set - clone vcpkg, run bootstrap-vcpkg.bat, "
                                      "then setx VCPKG_ROOT <folder>")
    toolchain = root / "scripts" / "buildsystems" / "vcpkg.cmake"
    if not toolchain.is_file():
        return Item("vcpkg", MISSING, f"{root} (VCPKG_ROOT) has no scripts/buildsystems/vcpkg.cmake")
    return Item("vcpkg", OK, f"{root} (VCPKG_ROOT)")


def check_vulkan_sdk():
    root, _ = setting("VULKAN_SDK")
    if root is None or not root.is_dir():
        return Item("Vulkan SDK", MISSING, "VULKAN_SDK not set - install it from "
                                           "https://vulkan.lunarg.com/")
    return Item("Vulkan SDK", OK, f"{root} (VULKAN_SDK)")


def check_checkout(name, env_name, default_name, marker, hint):
    """A borrowed tool's source checkout: its setting, else a folder beside this repository."""
    path, source = setting(env_name, REPO_ROOT.parent / default_name)
    if (path / marker).is_file():
        return Item(name, OK, f"{path} ({source})")
    return Item(name, MISSING, f"{path} ({source}) has no {marker} - {hint}")


def check_php():
    path = shutil.which("php")
    if not path:
        return Item("PHP 8.3+", MISSING, "not on PATH - https://www.php.net/downloads")
    version = run_version([path, "-r", "echo PHP_VERSION;"])
    try:
        too_old = tuple(int(n) for n in version.split(".")[:2]) < (8, 3)
    except ValueError:
        too_old = False
    if too_old:
        return Item("PHP 8.3+", MISSING, f"{version} is too old ({path})")
    return Item("PHP 8.3+", OK, f"{version}  ({path})")


def find_pngquant():
    for candidate in (TOOLS_DIR / "worldmap-builder" / "pngquant.exe",
                      TOOLS_DIR / "worldmap-builder" / "pngquant" / "pngquant.exe"):
        if candidate.is_file():
            return candidate
    return shutil.which("pngquant")


def find_rustydemon():
    env = os.environ.get("RUSTYDEMON_CLI")
    if env:
        return Path(env) if Path(env).is_file() else None
    on_path = shutil.which("rustydemon-cli")
    if on_path:
        return on_path
    beside = REPO_ROOT.parent / "rustydemon" / "target" / "release" / "rustydemon-cli.exe"
    return beside if beside.is_file() else None


def find_blender():
    on_path = shutil.which("blender")
    if on_path:
        return on_path
    root = os.environ.get("ProgramFiles", "C:/Program Files")
    hits = glob.glob(os.path.join(root, "Blender Foundation", "Blender *", "blender.exe"))

    # Newest install wins, compared as numbers so 4.10 beats 4.2
    def key(path):
        ver = Path(path).parent.name.replace("Blender", "").strip()
        return tuple(int(n) if n.isdigit() else 0 for n in ver.split("."))
    return max(hits, key=key) if hits else None


def find_blender_addon():
    appdata = os.environ.get("APPDATA")
    if not appdata:
        return []
    pattern = os.path.join(appdata, "Blender Foundation", "Blender", "*",
                           "scripts", "addons", "io_scene_wowobj")
    return [Path(p).parts[-4] for p in glob.glob(pattern)]


def setting(env_name, default=None):
    """(path, where it came from) for a folder or file setting."""
    value = os.environ.get(env_name, "").strip()
    if value:
        return Path(value), env_name
    return (Path(default), "default") if default else (None, "not set")


def wow_products(wow_dir):
    """Product -> version from the install's .build.info, or {} if unreadable."""
    info = wow_dir / ".build.info"
    try:
        lines = info.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return {}
    if not lines:
        return {}
    header = [h.split("!")[0] for h in lines[0].split("|")]
    if "Product" not in header or "Version" not in header:
        return {}
    p, v = header.index("Product"), header.index("Version")
    products = {}
    for line in lines[1:]:
        cols = line.split("|")
        if len(cols) > max(p, v) and cols[p]:
            products[cols[p]] = cols[v]
    return products


def check_wow():
    wow_dir, source = setting("AWOWLAB_WOW_DIR", DEFAULT_WOW_DIR)
    if not wow_dir.is_dir():
        return Item("WoW install", MISSING,
                    f"{wow_dir} ({source}) not found - set AWOWLAB_WOW_DIR")
    products = wow_products(wow_dir)
    listing = ", ".join(f"{k} {v}" for k, v in sorted(products.items())) or "no .build.info"
    return Item("WoW install", OK, f"{wow_dir} ({source}): {listing}")


def latest_layout_version():
    with open(db2_layouts.LAYOUTS_PATH, "r", encoding="utf-8") as f:
        versions = json.load(f)["versions"]

    def key(v):
        return tuple(int(n) if n.isdigit() else 0 for n in v.split("."))
    return max(versions, key=key), versions


def check_db2():
    """The export folder, then its layout check against the newest recorded version."""
    version, versions = latest_layout_version()
    db2_dir = db2_layouts.default_db2_dir(version)
    source = db2_layouts.DB2_DIR_ENV if os.environ.get(db2_layouts.DB2_DIR_ENV, "").strip() \
        else "default"

    has_exports = db2_dir.is_dir() and next(db2_dir.glob("*.txt"), None) is not None
    if not has_exports:
        problem = "not found" if not db2_dir.is_dir() else "holds no .txt exports"
        folder = Item("DB2 exports", MISSING,
                      f"{db2_dir} ({source}) {problem} - set {db2_layouts.DB2_DIR_ENV}")
        return [folder, Item(f"layouts {version}", MISSING, "no exports to check")]

    folder = Item("DB2 exports", OK, f"{db2_dir} ({source})")
    missing, changed = compare_layouts(db2_dir, version, versions[version])
    tables = versions[version]

    matched = len(tables) - len(missing) - len(changed)
    detail = f"{matched}/{len(tables)} tables match"
    if changed:
        detail += f"; changed: {', '.join(changed)}"
    if missing:
        detail += f"; missing: {', '.join(missing)}"
    if missing:
        status = MISSING
    elif changed:
        status = CHANGED
        # Exports from an older patch are the usual cause; name it if one fits.
        for other in sorted(versions, reverse=True):
            if other != version and compare_layouts(db2_dir, other, versions[other]) == ([], []):
                detail += f"; they match {other} exactly"
                break
    else:
        status = OK
    layout = Item(f"layouts {version}", status, detail)
    if status != OK:
        layout.detail += (f"  (python tools/db2_layouts.py --check <exports> --version {version}"
                          " for details)")
    return [folder, layout]


def compare_layouts(db2_dir, version, tables):
    """(missing tables, tables whose shape differs) against one recorded version."""
    missing, changed = [], []
    for table in sorted(tables):
        path = db2_layouts.find_export(db2_dir, table)
        if path is None:
            missing.append(table)
            continue
        try:
            db2_layouts.verify_layout(path, table, version)
        except db2_layouts.LayoutMismatch:
            changed.append(table)
    return missing, changed


def check_listfile():
    path, source = setting("AWOWLAB_LISTFILE",
                           TOOLS_DIR / "worldmap-builder" / "season_12.1_listfile.csv")
    if path.is_file():
        return Item("tile listfile", OK, f"{path} ({source})")
    return Item("tile listfile", OPTIONAL,
                f"{path} ({source}) not generated yet - made from list_required_files.py output")


def check_mmap_node():
    """mmap.node for the scripts in tools/ that drive wow.export: the setting,
    else the copy in the checkout's src folder."""
    checkout, _ = setting("WOWEXPORT_SRC", REPO_ROOT.parent / "wowexport-src")
    path, source = setting("WOWEXPORT_MMAP_NODE", checkout / "src" / "mmap.node")
    if path.is_file():
        return Item("mmap.node", OK, f"{path} ({source})")
    return Item("mmap.node", MISSING,
                f"{path} ({source}) not found - copy it from an installed wow.export "
                "or set WOWEXPORT_MMAP_NODE")


def check_icon_manifest():
    """The manifestinterfacedata export export_icons.py finds the icons with."""
    version, _ = latest_layout_version()
    db2_dir = db2_layouts.default_db2_dir(version)
    path = db2_layouts.find_export(db2_dir, "manifestinterfacedata")
    if path is not None:
        return Item("manifestinterfacedata", OK, str(path))
    return Item("manifestinterfacedata", MISSING,
                f"not in {db2_dir} - run tools/db2-export/export_db2.py, "
                "or pass --listfile to export_icons.py")


def check_file_setting(name, env_name, hint, optional=False):
    path, source = setting(env_name)
    if path is None:
        return Item(name, OPTIONAL if optional else MISSING, f"{env_name} not set - {hint}")
    if not path.is_file():
        return Item(name, MISSING, f"{path} ({env_name}) does not exist")
    return Item(name, OK, f"{path} ({env_name})")


def check_present(name, path, hint):
    """Existence only. These hold secrets, so the contents are never read."""
    if path.is_file():
        return Item(name, OK, f"present ({path.relative_to(REPO_ROOT)})")
    return Item(name, MISSING, hint)


def check_config():
    """awowlab.config.json: present, kept out of git, and holding a settings key.
    The key itself is never printed."""
    rel = "awowlab.config.json"
    path = REPO_ROOT / rel
    if not path.is_file():
        return [Item(rel, MISSING,
                     "the first CMake configure creates it from awowlab.config.example.json")]
    items = [Item(rel, OK, str(path))]

    try:
        ignored = subprocess.run(["git", "check-ignore", "-q", rel], cwd=REPO_ROOT,
                                 capture_output=True, timeout=20).returncode
        tracked = subprocess.run(["git", "ls-files", "--error-unmatch", rel], cwd=REPO_ROOT,
                                 capture_output=True, timeout=20).returncode == 0
    except (OSError, subprocess.SubprocessError):
        ignored, tracked = None, False
    if ignored is None:
        items.append(Item("gitignored", MISSING, "git did not run, so this was not checked"))
    elif tracked:
        items.append(Item("gitignored", MISSING,
                          f"{rel} is committed - git rm --cached {rel} and keep it to yourself"))
    elif ignored == 0:
        items.append(Item("gitignored", OK, f"git check-ignore {rel}"))
    else:
        items.append(Item("gitignored", MISSING, f"add /{rel} to .gitignore; it holds your keys"))

    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        items.append(Item("settings key", MISSING, f"{rel} could not be read: {exc}"))
        return items
    key = data.get("settings_key")
    if not isinstance(key, str) or not key:
        items.append(Item("settings key", MISSING,
                          f"settings_key in {rel} is empty - configure with CMake to fill it"))
    elif data.get("settings_key_generated") is True:
        items.append(Item("settings key", WARNING,
                          "generated for this checkout: fine for your own builds, but a release "
                          "needs the release key (tools/release/release.py refuses this one)"))
    else:
        items.append(Item("settings key", OK, "set"))
    return items


# --- report ------------------------------------------------------------------

def build_report():
    db2 = check_db2()
    wow = check_wow()
    pillow = check_package("Pillow")
    pngquant_opt = found("pngquant", find_pngquant(),
                         "for compression only - https://pngquant.org/, on PATH or "
                         "tools/worldmap-builder/pngquant.exe", optional=True)
    pngquant = found("pngquant", find_pngquant(),
                     "https://pngquant.org/, on PATH or tools/worldmap-builder/pngquant.exe")
    node = check_program("node", "node", "runs the patched wow.export scripts (bun cannot load "
                         "them) - https://nodejs.org/")
    mmap = check_file_setting("wow.export", "WOWEXPORT_MMAP_NODE",
                              "set it to mmap.node from an installed wow.export; the "
                              "patched build is in tools/external/wow.export/README.md")
    rustydemon = found("rustydemon-cli", find_rustydemon(),
                       "set RUSTYDEMON_CLI or put it on PATH - "
                       "https://github.com/HoldMyBeer-gg/rustydemon")
    cargo = check_program("cargo", "cargo", "only to build rustydemon-cli - https://rustup.rs/",
                          optional=rustydemon.status == OK)
    blender = found("Blender", find_blender(),
                    "4.2 or later, not on PATH or in Program Files - https://www.blender.org/download/")
    addons = find_blender_addon()
    addon = Item("io_scene_wowobj", OK, "installed for Blender " + ", ".join(addons)) if addons \
        else Item("io_scene_wowobj", MISSING,
                  "copy the patched addon into Blender's addons folder "
                  "(tools/external/wow.export/README.md)")
    translate = TOOLS_DIR / "translate"

    vs = find_visual_studio()
    return [
        ("Build", [
            Item("Visual Studio 2022", OK, vs) if vs else
            Item("Visual Studio 2022", MISSING,
                 "with the \"Desktop development with C++\" workload"),
            check_program("CMake", "cmake", "comes with Visual Studio; run from a Developer "
                          "prompt or add it to PATH"),
            check_vcpkg(),
            check_vulkan_sdk(),
        ]),
        ("Config", check_config()),
        ("DB2 export", [
            wow,
            node,
            check_checkout("wow.export checkout", "WOWEXPORT_SRC", "wowexport-src",
                           "package.json", "see tools/external/wow.export/README.md"),
            check_php(),
            check_checkout("db2reader checkout", "AWOWLAB_DB2READER", "db2reader",
                           "export_batch.php", "see tools/external/db2reader/README.md"),
        ]),
        ("Spell data", db2),
        ("Spell names", db2),
        ("Spell icons", db2 + [
            wow,
            node,
            check_checkout("wow.export checkout", "WOWEXPORT_SRC", "wowexport-src",
                           "package.json", "see tools/external/wow.export/README.md"),
            check_mmap_node(),
            check_icon_manifest(),
            pngquant,
        ]),
        ("Map bounds", db2),
        ("World maps", [wow, rustydemon, cargo, check_listfile(), pillow, pngquant_opt]),
        ("Arena maps", [blender, addon, mmap, node, wow, pillow, pngquant]),
        ("Models", [mmap, node, wow]),
        ("Translations", [
            check_package("deepl"),
            check_present("DeepL key", translate / "config.json",
                          "tools/translate/config.json - see tools/translate/README.md"),
        ]),
        ("Release packaging", [
            check_program("gh", "gh", "GitHub CLI - https://cli.github.com/"),
            check_package("cryptography"),
            check_file_setting("signing key", "AWOWLAB_SIGNING_KEY",
                               "needed to publish: the updater refuses unsigned releases, "
                               "see tools/release/SIGNING.md",
                               optional=True),
        ]),
    ]


def main():
    print("AwowLab tool setup")
    print("==================")
    print(f"{platform.system()} {platform.release()}, repository {REPO_ROOT}")
    python = check_python()
    print()
    print(f"  {python.status:<9}{python.name:<26}{python.detail}")

    report = build_report()
    ready = []
    for step, items in report:
        print()
        print(step)
        for item in items:
            print(f"  {item.status:<9}{item.name:<26}{item.detail}")
        if all(i.status not in BLOCKING for i in items) and python.status == OK:
            ready.append(step)

    print()
    print("Summary")
    for step, _ in report:
        print(f"  {'ready' if step in ready else 'not ready':<11}{step}")
    print()
    print("Settings are environment variables; see docs/UPDATE-FLOW.md. "
          "Steps whose output is committed can be skipped.")
    return 0


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:  # informational only: never fail the caller
        print(f"\ncheck_setup stopped early: {exc!r}")
    sys.exit(0)
