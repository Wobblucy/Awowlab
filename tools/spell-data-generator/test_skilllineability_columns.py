#!/usr/bin/env python3
"""
Guard the version-dependent column positions in parse_skilllineability.

skilllineability moves its columns between game versions: 12.1 dropped the
leading RaceMask field, sliding SkillLine and Spell one position left while the
field count stayed at 18. A stale index therefore reads Spell as SkillLine and a
flag as Spell, yielding wrong class spell lists with no error raised. These tests
pin the layout-hash -> column mapping so that regression cannot return unnoticed.

Run:  python tools/spell-data-generator/test_skilllineability_columns.py
"""

import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from extract_spell_gcd import parse_skilllineability, SKILLLINEABILITY_COLUMNS
from db2_layouts import LayoutMismatch, default_db2_dir, find_export

MAGE_SKILL_LINE = 904

# One real row per version, same ability (id 4991, skill line 165, spell 9064).
# 12.0 leads with RaceMask=0; 12.1 drops it and gains a [0,0] array near the end.
ROW_12_0 = "0,,,4991,165,9064,1,0,0,0,95,65,0,1,71,257,2532,165"
ROW_12_1 = ",,4991,165,9064,1,0,0,0,95,65,0,1,71,257,2532,[0,0],165"


def write_export(layout_hash, rows):
    tmp = Path(tempfile.mkdtemp()) / "skilllineability.txt"
    tmp.write_text(
        f"Layout: {layout_hash}\n" + "".join(f"{i}: {r}\n" for i, r in enumerate(rows, 1)),
        encoding="utf-8")
    return tmp


def test_both_versions_agree():
    """The same ability must parse to the same (skillline, spell) in either version."""
    v120 = parse_skilllineability(write_export("c070825e", [ROW_12_0]))
    v121 = parse_skilllineability(write_export("f98aa48e", [ROW_12_1]))

    assert v120 == {165: {9064}}, f"12.0 parsed wrong: {dict(v120)}"
    assert v121 == {165: {9064}}, f"12.1 parsed wrong: {dict(v121)}"
    assert v120 == v121, "same ability parsed differently across versions"
    print("PASS: identical ability parses the same under 12.0 and 12.1")


def test_stale_indices_would_be_wrong():
    """
    Confirm the shift is real: reading 12.1 data at 12.0's positions produces the
    silent corruption this mapping exists to prevent.
    """
    fields = ROW_12_1.split(",")
    stale = SKILLLINEABILITY_COLUMNS["c070825e"]
    wrong_skillline = fields[stale["skillline"]]
    wrong_spell = fields[stale["spell"]]

    assert wrong_skillline == "9064", f"expected the spell id to land in the skillline slot, got {wrong_skillline}"
    assert wrong_spell == "1", f"expected a flag to land in the spell slot, got {wrong_spell}"
    assert wrong_skillline != "165", "stale index must not accidentally still be correct"
    print("PASS: 12.0 indices on 12.1 data would read spell-as-skillline (silent corruption)")


def test_unknown_layout_fails_loudly():
    """An unseen shape must raise, not guess."""
    try:
        parse_skilllineability(write_export("deadbeef", [ROW_12_1]))
    except LayoutMismatch as exc:
        assert "deadbeef" in str(exc)
        print("PASS: unknown layout hash raises LayoutMismatch")
        return
    raise AssertionError("FAIL: unknown layout hash was parsed anyway")


def test_real_exports_if_present():
    """
    Against a real dump, class spell lists must stay stable across versions.
    Reads AWOWLAB_DB2_DIR, else rawdb2/12.0, and skips when neither has one.
    """
    export_path = find_export(default_db2_dir("12.0"), "skilllineability")
    if export_path is None:
        print("SKIP: no real skilllineability export (set AWOWLAB_DB2_DIR to run this)")
        return

    mage = parse_skilllineability(export_path).get(MAGE_SKILL_LINE, set())
    # Frostbolt, Polymorph, Arcane Intellect, Counterspell - stable Mage abilities.
    for spell_id in (116, 118, 1459, 2139):
        assert spell_id in mage, f"expected spell {spell_id} in the Mage skill line"
    assert len(mage) > 50, f"Mage list implausibly small ({len(mage)}) - columns likely misread"
    print(f"PASS: real export yields a plausible Mage list ({len(mage)} spells)")


if __name__ == "__main__":
    test_both_versions_agree()
    test_stale_indices_would_be_wrong()
    test_unknown_layout_fails_loudly()
    test_real_exports_if_present()
    print("\nAll skilllineability column tests passed.")
