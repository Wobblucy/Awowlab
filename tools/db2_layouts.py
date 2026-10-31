#!/usr/bin/env python3
"""
Shared DB2 export access: locate an export file and confirm its shape.

The exports are positional dumps. Line 1 is "Layout: <hash>", every later line is
"<id>: field,field,...". There are no column names, so tools read fields by index.
A column added or reordered upstream shifts every index after it, and the result
still parses -- it just produces wrong numbers. The layout hash on line 1 is the
structure fingerprint that makes such a change detectable, so check it before
trusting a parse.

Typical use:

    from db2_layouts import open_export

    with open_export(db2_dir, "spellcooldowns", version="12.1") as f:
        for line in f:
            ...  # header already consumed

open_export() finds the file under either naming convention, verifies the hash
against tools/db2_layouts.json, and leaves the handle positioned on the first
data row.

Where the exports live: a tool takes the folder on its command line when it has
an argument for it. Without one it reads AWOWLAB_DB2_DIR, and without that it
looks in rawdb2/<version> at the repository root. resolve_db2_dir() applies that
order and stops with a message naming both settings when nothing is there.

CLI:
    python db2_layouts.py --scan <export_dir> [--version 12.1]
        Print a JSON block of every table's hash, for pasting into db2_layouts.json.

    python db2_layouts.py --check <export_dir> --version 12.1
        Verify an export dir against the recorded hashes. Exit 1 on mismatch.

    python db2_layouts.py --diff 12.0 12.1
        List tables whose shape changed between two recorded versions.
"""

import json
import os
import re
import struct
import sys
from pathlib import Path

LAYOUTS_PATH = Path(__file__).parent / "db2_layouts.json"
REPO_ROOT = Path(__file__).resolve().parent.parent

# Environment variable naming the DB2 text export folder, for tools run without
# the folder on their command line.
DB2_DIR_ENV = "AWOWLAB_DB2_DIR"

# The game version the tools target by default. Bumping this is the single edit a
# new patch needs: it names the generated files and selects which recorded column
# layout the exports are checked against. Individual tools accept --version to
# override it, which is how you regenerate data for an older version.
DEFAULT_VERSION = "12.1"

_LAYOUT_RE = re.compile(r"Layout:\s*([0-9a-fA-F]+)")

# WDC5 header: magic(4) + version(4) + schema string(128), then the field block
# whose 6th uint32 is the layout hash.
_WDC5_HEADER_OFFSET = 136


class LayoutMismatch(Exception):
    """An export's shape does not match what the tools were written against."""


def _load_layouts():
    with open(LAYOUTS_PATH, "r", encoding="utf-8") as f:
        return json.load(f)


def find_export(db2_dir, base_name):
    """
    Locate an export, accepting either naming convention:
      {base_name}.txt         - what the current exporter produces
      {base_name}_export.txt  - older exports still found in some dumps
    Returns a Path, or None if neither exists.
    """
    db2_dir = Path(db2_dir)
    for candidate in (db2_dir / f"{base_name}.txt", db2_dir / f"{base_name}_export.txt"):
        if candidate.exists():
            return candidate
    return None


def default_db2_dir(version=DEFAULT_VERSION):
    """
    The export folder a tool uses when none is passed: AWOWLAB_DB2_DIR if set,
    else rawdb2/<version> at the repository root. Not checked for existence.
    """
    env = os.environ.get(DB2_DIR_ENV, "").strip()
    return Path(env) if env else REPO_ROOT / "rawdb2" / version


def resolve_db2_dir(path=None, version=DEFAULT_VERSION):
    """
    Return the export folder to read: path if given, else default_db2_dir().
    Exits with a message saying how to point the tools at the exports when the
    folder is missing or holds no .txt exports.
    """
    if path:
        db2_dir, source = Path(path), "the command line"
    elif os.environ.get(DB2_DIR_ENV, "").strip():
        db2_dir, source = default_db2_dir(version), DB2_DIR_ENV
    else:
        db2_dir, source = default_db2_dir(version), "the default location"

    if db2_dir.is_dir() and next(db2_dir.glob("*.txt"), None) is not None:
        return db2_dir

    problem = "does not exist" if not db2_dir.is_dir() else "has no .txt exports in it"
    raise SystemExit(
        f"DB2 export folder {problem}: {db2_dir}  (from {source})\n"
        f"  Pass the export folder on the command line, or set {DB2_DIR_ENV} to it.\n"
        f"  The exports come from db2reader; see docs/UPDATE-FLOW.md, step 1."
    )


def read_layout_hash(path):
    """Return the layout hash from an export's header, or None if absent."""
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        match = _LAYOUT_RE.match(f.readline().strip())
    return match.group(1).lower().zfill(8) if match else None


def read_db2_layout_hash(db2_path):
    """Return the layout hash straight from a raw .db2 (WDC5) header."""
    with open(db2_path, "rb") as f:
        header = f.read(_WDC5_HEADER_OFFSET + 24)
    if header[:4] != b"WDC5":
        raise ValueError(f"{db2_path}: not a WDC5 file (magic {header[:4]!r})")
    layout_hash = struct.unpack("<6I", header[_WDC5_HEADER_OFFSET:_WDC5_HEADER_OFFSET + 24])[5]
    return "%08x" % layout_hash


def expected_hash(base_name, version):
    """The recorded hash for a table at a version, or None if unrecorded."""
    return _load_layouts()["versions"].get(version, {}).get(base_name)


def verify_layout(path, base_name, version):
    """
    Raise LayoutMismatch if the export's shape differs from the recorded hash.
    Unrecorded tables and headerless files pass; only a definite mismatch fails.
    """
    expected = expected_hash(base_name, version)
    if expected is None:
        return
    actual = read_layout_hash(path)
    if actual is None or actual == expected:
        return
    raise LayoutMismatch(
        f"\n{base_name}: this table's shape changed.\n"
        f"  file:     {path}\n"
        f"  expected: {expected}  (recorded for {version} in db2_layouts.json)\n"
        f"  found:    {actual}\n\n"
        f"  Columns were added or reordered upstream, so the field positions this\n"
        f"  tool reads no longer line up. Parsing anyway would produce wrong values\n"
        f"  without failing. Remap this table's column indices, then record the new\n"
        f"  hash in db2_layouts.json.\n"
    )


def open_export(db2_dir, base_name, version, required=True):
    """
    Open an export ready to read, with its shape verified and header consumed.
    Returns an open handle positioned on the first data row, or None when the
    file is absent and required=False.
    """
    path = find_export(db2_dir, base_name)
    if path is None:
        if not required:
            return None
        raise FileNotFoundError(
            f"{base_name}: no export found in {db2_dir}\n"
            f"  looked for {base_name}.txt and {base_name}_export.txt"
        )

    verify_layout(path, base_name, version)

    handle = open(path, "r", encoding="utf-8", errors="replace")
    first = handle.readline()
    if not _LAYOUT_RE.match(first.strip()):
        handle.seek(0)  # no header; the first line is data
    return handle


# --- CLI ---------------------------------------------------------------------

def _scan(export_dir, version):
    export_dir = Path(export_dir)
    known = _load_layouts()["versions"].get(version) or _load_layouts()["versions"]["12.0"]
    found = {}
    for base_name in sorted(known):
        path = find_export(export_dir, base_name)
        if path is None:
            print(f"  MISSING {base_name}", file=sys.stderr)
            continue
        found[base_name] = read_layout_hash(path)
    print(json.dumps({version: found}, indent=2))


def _check(export_dir, version):
    known = _load_layouts()["versions"].get(version)
    if known is None:
        print(f"No hashes recorded for version {version}", file=sys.stderr)
        return 1

    failures, missing = [], []
    for base_name in sorted(known):
        path = find_export(export_dir, base_name)
        if path is None:
            missing.append(base_name)
            continue
        try:
            verify_layout(path, base_name, version)
        except LayoutMismatch as exc:
            failures.append(str(exc))

    for name in missing:
        print(f"MISSING  {name}")
    for failure in failures:
        print(failure)

    checked = len(known) - len(missing)
    print(f"\n{checked - len(failures)}/{checked} tables match the recorded {version} layout")
    return 1 if (failures or missing) else 0


def _diff(old_version, new_version):
    versions = _load_layouts()["versions"]
    old, new = versions.get(old_version), versions.get(new_version)
    if not old or not new:
        print("Both versions must be recorded in db2_layouts.json", file=sys.stderr)
        return 1

    changed = [t for t in sorted(set(old) & set(new)) if old[t] != new[t]]
    for table in changed:
        print(f"CHANGED  {table:<26} {old[table]} -> {new[table]}")
    for table in sorted(set(new) - set(old)):
        print(f"NEW      {table}")
    for table in sorted(set(old) - set(new)):
        print(f"DROPPED  {table}")

    print(f"\n{len(changed)}/{len(set(old) & set(new))} shared tables changed shape "
          f"between {old_version} and {new_version}")
    return 0


def main(argv):
    if "--scan" in argv:
        idx = argv.index("--scan")
        version = argv[argv.index("--version") + 1] if "--version" in argv else "12.0"
        return _scan(argv[idx + 1], version) or 0
    if "--check" in argv:
        idx = argv.index("--check")
        if "--version" not in argv:
            print("--check requires --version", file=sys.stderr)
            return 2
        return _check(argv[idx + 1], argv[argv.index("--version") + 1])
    if "--diff" in argv:
        idx = argv.index("--diff")
        return _diff(argv[idx + 1], argv[idx + 2])

    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
