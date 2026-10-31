#!/usr/bin/env python3
"""
Export the game's DB2 tables from a local WoW install as the text exports every
script under tools/ reads.

Three steps in one run:
  1. Copy the raw .db2 files out of the install (dump_db2.js, using wow.export's
     CASC code without its window).
  2. Convert them to text with db2reader's export_batch.php.
  3. Check each table's layout hash against tools/db2_layouts.json.

By default only the tables the tools read are exported (tables.csv beside this
script). --all exports every table and needs a community listfile to name them.

Usage:
    python tools/db2-export/export_db2.py [--out <dir>] [--product wow]

Environment (each also has a flag, see --help):
    AWOWLAB_WOW_DIR      game install root   (C:/Program Files (x86)/World of Warcraft)
    AWOWLAB_DB2_DIR      where the text exports go   (rawdb2/<version>)
    WOWEXPORT_SRC        wow.export source checkout   (../wowexport-src beside this repo)
    WOWEXPORT_MMAP_NODE  mmap.node from a wow.export install   (<checkout>/src/mmap.node)
    WOWEXPORT_NODE       node executable   (node on PATH)
    AWOWLAB_DB2READER    db2reader checkout   (../db2reader beside this repo)
    PHP                  php executable   (php on PATH)
"""

import argparse
import csv
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE.parent))

from db2_layouts import DEFAULT_VERSION, default_db2_dir, expected_hash, find_export, read_layout_hash  # noqa: E402

DEFAULT_WOW_DIR = "C:/Program Files (x86)/World of Warcraft"
# Checkouts of the borrowed tools default to folders beside this repository.
DEFAULT_WOWEXPORT = str(REPO_ROOT.parent / "wowexport-src")
DEFAULT_DB2READER = str(REPO_ROOT.parent / "db2reader")
TABLE_LIST = HERE / "tables.csv"


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


def extract(args, list_path, raw_dir):
    """Run dump_db2.js. Returns (per-table results, build version)."""
    raw_dir.mkdir(parents=True, exist_ok=True)
    for stale in raw_dir.glob("*.db2"):
        stale.unlink()

    cmd = [args.node, str(HERE / "dump_db2.js"), args.wowexport, args.wow_dir,
           args.product, str(list_path), str(raw_dir)]
    if args.no_cdn:
        cmd.append("--no-cdn")

    results, version = [], None
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, encoding="utf-8", errors="replace")
    for line in proc.stdout:
        line = line.strip()
        if not line.startswith("{"):
            continue
        entry = json.loads(line)
        if entry.get("done"):
            version = entry.get("version")
        else:
            results.append(entry)
    err = proc.stderr.read()
    if proc.wait() != 0:
        sys.exit(f"dump_db2.js failed:\n{err.strip()}")
    return results, version


def convert(args, raw_dir, out_dir):
    """Run export_batch.php over raw_dir. Returns {table: error} for failures."""
    script = Path(args.db2reader) / "export_batch.php"
    if not script.exists():
        sys.exit(f"{script} not found. Copy it in from tools/external/db2reader/scripts/ "
                 f"(see tools/external/db2reader/README.md).")

    out_dir.mkdir(parents=True, exist_ok=True)
    # The largest tables need more than PHP's default 128 MB.
    proc = subprocess.run(
        [args.php, "-d", "memory_limit=-1", str(script),
         raw_dir.as_posix(), out_dir.as_posix(), "--keep-name"],
        capture_output=True, text=True, encoding="utf-8", errors="replace")

    failures, current = {}, None
    for line in proc.stdout.splitlines():
        if line.startswith("Processing: "):
            current = line[len("Processing: "):].strip()[:-len(".db2")].lower()
        elif line.startswith("[FAILED]") and current:
            failures[current] = line[len("[FAILED] Error: "):].strip()
    if proc.returncode != 0:
        tail = "\n".join((proc.stdout + proc.stderr).strip().splitlines()[-5:])
        sys.exit(f"export_batch.php stopped (exit {proc.returncode}):\n{tail}")
    return failures


def check_layouts(out_dir, names, version):
    """Sort exported tables into match / changed / unrecorded / missing."""
    report = {"match": [], "changed": [], "unrecorded": [], "missing": []}
    for name in names:
        path = find_export(out_dir, name)
        if path is None:
            report["missing"].append(name)
            continue
        expected = expected_hash(name, version)
        if expected is None:
            report["unrecorded"].append(name)
        elif read_layout_hash(path) == expected:
            report["match"].append(name)
        else:
            report["changed"].append((name, expected, read_layout_hash(path)))
    return report


def main():
    parser = argparse.ArgumentParser(
        description="Export DB2 tables from a WoW install as text exports.")
    parser.add_argument("--wow-dir", default=os.environ.get("AWOWLAB_WOW_DIR", DEFAULT_WOW_DIR),
                        help="game install root (env AWOWLAB_WOW_DIR)")
    parser.add_argument("--product", default="wow",
                        help="wow for retail, wowt for the PTR (default: wow)")
    parser.add_argument("--out", default=os.environ.get("AWOWLAB_DB2_DIR"),
                        help="folder for the text exports (env AWOWLAB_DB2_DIR, else rawdb2/<version>)")
    parser.add_argument("--raw-dir",
                        help="folder for the raw .db2 files (default: <out>/db2)")
    parser.add_argument("--all", action="store_true",
                        help="export every table, not just the ones the tools read")
    parser.add_argument("--listfile",
                        help="community listfile, needed with --all to name every table")
    parser.add_argument("--no-cdn", action="store_true",
                        help="fail a table rather than download it when the install lacks it")
    parser.add_argument("--version", default=DEFAULT_VERSION,
                        help=f"recorded layout to check against (default: {DEFAULT_VERSION})")
    parser.add_argument("--wowexport", help="wow.export source checkout (env WOWEXPORT_SRC)")
    parser.add_argument("--node", help="node executable (env WOWEXPORT_NODE)")
    parser.add_argument("--db2reader", help="db2reader checkout (env AWOWLAB_DB2READER)")
    parser.add_argument("--php", help="php executable (env PHP)")
    args = parser.parse_args()

    products = read_build_info(args.wow_dir)
    if products is None:
        sys.exit(f"No .build.info in {args.wow_dir}. Point --wow-dir or AWOWLAB_WOW_DIR at the install root.")
    if args.product not in products:
        listed = ", ".join(f"{p} ({v})" for p, v in products.items())
        sys.exit(f"Product {args.product} is not installed. Installed: {listed}")

    if args.all:
        if not args.listfile:
            sys.exit("--all needs --listfile <community-listfile.csv> to name every table "
                     "(https://github.com/wowdev/wow-listfile).")
        list_path = Path(args.listfile)
    else:
        list_path = TABLE_LIST

    args.wowexport = find_tool(args.wowexport, "WOWEXPORT_SRC", DEFAULT_WOWEXPORT, "wow.export checkout")
    args.node = find_tool(args.node, "WOWEXPORT_NODE", "node", "node")
    args.db2reader = find_tool(args.db2reader, "AWOWLAB_DB2READER", DEFAULT_DB2READER, "db2reader checkout")
    args.php = find_tool(args.php, "PHP", "php", "php")

    out_dir = Path(args.out) if args.out else default_db2_dir(args.version)
    raw_dir = Path(args.raw_dir) if args.raw_dir else out_dir / "db2"

    print(f"Install:  {args.wow_dir}")
    print(f"Product:  {args.product} ({products[args.product]})")
    print(f"Tables:   {'every table in ' + str(list_path) if args.all else 'the ones the tools read'}")
    print(f"Output:   {out_dir}")
    print()

    started = time.time()
    print("Extracting...")
    results, version = extract(args, list_path, raw_dir)
    extracted = time.time()

    ok = [r for r in results if r["status"] == "ok"]
    failed = [r for r in results if r["status"] == "failed"]
    absent = [r for r in results if r["status"] == "absent"]
    from_cdn = [r for r in ok if r.get("source") == "cdn"]
    zeroed = [r for r in ok if r.get("zeroed")]
    print(f"  {len(ok)} extracted in {extracted - started:.0f}s")

    # Drop earlier exports of these tables first, so a table that fails this
    # time is reported missing instead of an old export passing for it.
    for r in results:
        for old in (out_dir / f"{r['table']}.txt", out_dir / f"{r['table']}_export.txt"):
            if old.exists():
                old.unlink()

    print("Converting...")
    convert_failures = convert(args, raw_dir, out_dir)
    converted = time.time()
    print(f"  {len(ok) - len(convert_failures)} converted in {converted - extracted:.0f}s")

    names = [r["table"] for r in results if r["status"] != "absent"]
    layouts = check_layouts(out_dir, names, args.version)

    print()
    print(f"Build {version or products[args.product]}, {converted - started:.0f}s total")
    if from_cdn:
        print(f"\nNot readable from the install, so taken from the CDN or wow.export's copy "
              f"of an earlier download ({len(from_cdn)}):")
        for r in from_cdn:
            print(f"  {r['table']}")
    if zeroed:
        print(f"\nEncrypted rows left blank, as CASC Explorer does ({len(zeroed)}): "
              + ", ".join(r["table"] for r in zeroed))
    if absent:
        print(f"\nListed but not in this build ({len(absent)}), skipped")
    if failed:
        print(f"\nCould not extract ({len(failed)}) - try CASC Explorer, see docs/CASC-EXTRACTION.md:")
        for r in failed:
            print(f"  {r['table']}: {r.get('error')}")
    if convert_failures:
        print(f"\nCould not convert ({len(convert_failures)}):")
        for name, error in sorted(convert_failures.items()):
            print(f"  {name}: {error}")

    recorded = len(layouts["match"]) + len(layouts["changed"])
    print(f"\nLayout check against {args.version}: {len(layouts['match'])}/{recorded} recorded tables match")
    for name, expected, found in layouts["changed"]:
        print(f"  CHANGED  {name:<26} {expected} -> {found}")
    for name in layouts["missing"]:
        print(f"  MISSING  {name}")
    if layouts["changed"]:
        print("\nChanged tables need their columns checked before any generator runs.")
        print("See docs/UPDATE-FLOW.md, Step 2.")

    return 1 if (failed or convert_failures or layouts["missing"]) else 0


if __name__ == "__main__":
    sys.exit(main())
