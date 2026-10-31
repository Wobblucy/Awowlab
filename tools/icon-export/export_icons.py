#!/usr/bin/env python3
"""
Export the game's icon images (interface/icons/*.blp) from a local WoW install
as PNG files named the way the app looks them up: lowercase, .png in place of
.blp.

The icons are found by fileDataID, so the script needs a list of them. It takes
the manifestinterfacedata export from Step 1 of docs/UPDATE-FLOW.md, which the
game itself uses to name its interface files. --listfile adds the community
listfile's icons to that, or replaces it when there is no export.

dump_icons.js does the extraction and decoding with wow.export's CASC reader and
BLP decoder, without its window.

Usage:
    python tools/icon-export/export_icons.py [--out <dir>] [--product wow]

Environment (each also has a flag, see --help):
    AWOWLAB_WOW_DIR      game install root   (C:/Program Files (x86)/World of Warcraft)
    AWOWLAB_DB2_DIR      DB2 text exports, for manifestinterfacedata   (rawdb2/<version>)
    WOWEXPORT_SRC        wow.export source checkout   (../wowexport-src beside this repo)
    WOWEXPORT_MMAP_NODE  mmap.node from a wow.export install   (<checkout>/src/mmap.node)
    WOWEXPORT_NODE       node executable   (node on PATH)
"""

import argparse
import csv
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE.parent))

from db2_layouts import DEFAULT_VERSION, default_db2_dir, find_export  # noqa: E402

DEFAULT_WOW_DIR = "C:/Program Files (x86)/World of Warcraft"
DEFAULT_WOWEXPORT = str(REPO_ROOT.parent / "wowexport-src")
DEFAULT_OUT = REPO_ROOT / "icon-pack" / "PNG" / "Spell Icons"
ICON_TABLE = REPO_ROOT / "AwowLab" / "data" / "spell_icon_table.bin"
REQUIRED_ICONS = REPO_ROOT / "tools" / "icon-culler" / "required_icons.txt"

ICON_TABLE_MAGIC = 0x49434F4E  # "ICON"


def read_build_info(wow_dir):
    """Product -> version for every product in the install's .build.info."""
    path = Path(wow_dir) / ".build.info"
    if not path.exists():
        return None
    with open(path, encoding="utf-8") as f:
        rows = [r for r in csv.reader(f, delimiter="|") if r]
    header = [h.split("!")[0] for h in rows[0]]
    products = {}
    for row in rows[1:]:
        entry = dict(zip(header, row))
        products[entry.get("Product")] = entry.get("Version")
    return products


def find_tool(given, env_name, default, label):
    """Resolve a tool from its flag, then its environment variable, then a default."""
    value = given or os.environ.get(env_name) or default
    if value and (Path(value).exists() or shutil.which(value)):
        return shutil.which(value) or value
    sys.exit(f"{label} not found ({value or 'not set'}). Pass its flag or set {env_name}.")


def png_name(blp_name):
    """Interface file name -> the name the app loads: lowercase, .blp -> .png."""
    name = blp_name.strip().lower()
    return (name[:-4] if name.endswith(".blp") else name) + ".png"


def icons_from_listfile(path):
    """{fileDataID: png name} for every interface/icons/*.blp line of a listfile."""
    pattern = re.compile(r"^(\d+);interface[\\/]icons[\\/]([^\\/]+\.blp)$", re.IGNORECASE)
    icons = {}
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            match = pattern.match(line.strip())
            if match:
                icons[int(match.group(1))] = png_name(match.group(2))
    return icons


def icons_from_manifest(path):
    """{fileDataID: png name} for every Interface\\ICONS\\ row of manifestinterfacedata.

    Rows look like "136243: Interface\\ICONS\\,Trade_Engineering.blp"; the row id
    is the fileDataID.
    """
    pattern = re.compile(r"^(\d+):\s*interface[\\/]icons[\\/]?,(.+\.blp)$", re.IGNORECASE)
    icons = {}
    with open(path, encoding="utf-8-sig", errors="replace") as f:
        for line in f:
            match = pattern.match(line.strip())
            if match:
                icons[int(match.group(1))] = png_name(match.group(2))
    return icons


def read_icon_table(path):
    """Every icon file name spell_icon_table.bin refers to."""
    data = Path(path).read_bytes()
    magic, _version, count, _mappings = struct.unpack_from("<IIII", data, 0)
    if magic != ICON_TABLE_MAGIC:
        sys.exit(f"{path} is not a spell icon table.")
    names, ofs = set(), 16
    for _ in range(count):
        (length,) = struct.unpack_from("<H", data, ofs)
        names.add(data[ofs + 2:ofs + 2 + length].decode("utf-8"))
        ofs += 2 + length
    return names


def read_required_icons(path):
    """Icons the app loads by name rather than through a spell, one per line."""
    if not Path(path).exists():
        return set()
    with open(path, encoding="utf-8") as f:
        return {line.strip().lower() for line in f
                if line.strip() and not line.startswith("#")}


def extract(args, icons, out_dir):
    """Run dump_icons.js over icons. Returns (problem lines, final summary)."""
    out_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile("w", suffix=".csv", delete=False, encoding="utf-8") as f:
        for fdid, name in sorted(icons.items()):
            f.write(f"{fdid};interface/icons/{name[:-4]}.blp\n")
        list_path = f.name

    cmd = [args.node, str(HERE / "dump_icons.js"), args.wowexport, args.wow_dir,
           args.product, list_path, str(out_dir)]
    if args.no_cdn:
        cmd.append("--no-cdn")

    problems, summary = [], None
    try:
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True, encoding="utf-8", errors="replace")
        for line in proc.stdout:
            line = line.strip()
            if not line.startswith("{"):
                continue
            entry = json.loads(line)
            if entry.get("done"):
                summary = entry
            elif "progress" in entry:
                print(f"  {entry['progress']}/{entry['total']}", flush=True)
            else:
                problems.append(entry)
        err = proc.stderr.read()
        if proc.wait() != 0:
            sys.exit(f"dump_icons.js failed:\n{err.strip()}")
    finally:
        os.unlink(list_path)
    return problems, summary


def main():
    parser = argparse.ArgumentParser(
        description="Export the game's icons from a WoW install as PNG.")
    parser.add_argument("--wow-dir", default=os.environ.get("AWOWLAB_WOW_DIR", DEFAULT_WOW_DIR),
                        help="game install root (env AWOWLAB_WOW_DIR)")
    parser.add_argument("--product", default="wow",
                        help="wow for retail, wowt for the PTR (default: wow)")
    parser.add_argument("--out", default=str(DEFAULT_OUT),
                        help="folder for the PNG files (default: icon-pack/PNG/Spell Icons)")
    parser.add_argument("--db2-dir",
                        help="DB2 text exports holding manifestinterfacedata "
                             "(env AWOWLAB_DB2_DIR, else rawdb2/<version>)")
    parser.add_argument("--listfile",
                        help="community listfile to find the icons with, instead of manifestinterfacedata")
    parser.add_argument("--only-needed", action="store_true",
                        help="export only the icons spell_icon_table.bin and required_icons.txt name")
    parser.add_argument("--icon-table", default=str(ICON_TABLE),
                        help="spell icon table read by --only-needed (default: AwowLab/data/spell_icon_table.bin)")
    parser.add_argument("--no-cdn", action="store_true",
                        help="fail an icon rather than download it when the install lacks it")
    parser.add_argument("--wowexport", help="wow.export source checkout (env WOWEXPORT_SRC)")
    parser.add_argument("--node", help="node executable (env WOWEXPORT_NODE)")
    args = parser.parse_args()

    products = read_build_info(args.wow_dir)
    if products is None:
        sys.exit(f"No .build.info in {args.wow_dir}. Point --wow-dir or AWOWLAB_WOW_DIR at the install root.")
    if args.product not in products:
        listed = ", ".join(f"{p} ({v})" for p, v in products.items())
        sys.exit(f"Product {args.product} is not installed. Installed: {listed}")

    # The manifest is the game's own list and is current for the build; the
    # community listfile also names a few icons the manifest leaves out. With
    # both, every icon either one names is exported.
    db2_dir = Path(args.db2_dir) if args.db2_dir else default_db2_dir(DEFAULT_VERSION)
    manifest = find_export(db2_dir, "manifestinterfacedata")
    if manifest is None and not args.listfile:
        sys.exit(f"No manifestinterfacedata export in {db2_dir}.\n"
                 f"  Run tools/db2-export/export_db2.py first (docs/UPDATE-FLOW.md, Step 1), "
                 f"pass --db2-dir, or pass --listfile <community-listfile.csv>.")
    icons, sources = {}, []
    if args.listfile:
        listed = icons_from_listfile(args.listfile)
        icons.update(listed)
        sources.append(f"{len(listed)} in {args.listfile}")
    if manifest is not None:
        listed = icons_from_manifest(manifest)
        icons.update(listed)
        sources.append(f"{len(listed)} in {manifest}")
    if not icons:
        sys.exit("No interface/icons entries in " + " or ".join(sources))

    missing_needed = []
    if args.only_needed:
        if not Path(args.icon_table).exists():
            sys.exit(f"{args.icon_table} not found. Build it first (docs/UPDATE-FLOW.md, Step 4) "
                     f"or drop --only-needed.")
        needed = read_icon_table(args.icon_table) | read_required_icons(REQUIRED_ICONS)
        available = set(icons.values())
        missing_needed = sorted(needed - available)
        icons = {fdid: name for fdid, name in icons.items() if name in needed}

    args.wowexport = find_tool(args.wowexport, "WOWEXPORT_SRC", DEFAULT_WOWEXPORT, "wow.export checkout")
    args.node = find_tool(args.node, "WOWEXPORT_NODE", "node", "node")
    out_dir = Path(args.out)

    print(f"Install:  {args.wow_dir}")
    print(f"Product:  {args.product} ({products[args.product]})")
    print(f"Icons:    {len(icons)}{', the ones the app uses' if args.only_needed else ''}")
    for line in sources:
        print(f"          {line}")
    print(f"Output:   {out_dir}")
    print()

    started = time.time()
    print("Exporting...")
    problems, summary = extract(args, icons, out_dir)
    elapsed = time.time() - started

    from_cdn = [p for p in problems if p["status"] == "cdn"]
    encrypted = [p for p in problems if p["status"] == "encrypted"]
    failed = [p for p in problems if p["status"] == "failed"]

    print()
    print(f"Build {summary['version']}: {summary['written']} of {summary['listed']} icons "
          f"written in {elapsed:.0f}s")
    if summary["absent"]:
        print(f"\nListed but not in this build ({summary['absent']}), skipped")
    if from_cdn:
        print(f"\nNot readable from the install, so taken from the CDN or wow.export's copy "
              f"of an earlier download ({len(from_cdn)}):")
        for p in from_cdn[:20]:
            print(f"  {p['icon']}")
        if len(from_cdn) > 20:
            print(f"  ... and {len(from_cdn) - 20} more")
    if encrypted:
        print(f"\nEncrypted for content the client has not unlocked ({len(encrypted)}), skipped:")
        for p in encrypted[:20]:
            print(f"  {p['icon']}")
        if len(encrypted) > 20:
            print(f"  ... and {len(encrypted) - 20} more")
    if failed:
        print(f"\nCould not export ({len(failed)}) - try CASC Explorer, see docs/CASC-EXTRACTION.md:")
        for p in failed:
            print(f"  {p['icon']} ({p['fdid']}): {p.get('error')}")
    if missing_needed:
        print(f"\nNamed by the icon table but not listed by the game ({len(missing_needed)}):")
        for name in missing_needed[:20]:
            print(f"  {name}")
        if len(missing_needed) > 20:
            print(f"  ... and {len(missing_needed) - 20} more")

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
