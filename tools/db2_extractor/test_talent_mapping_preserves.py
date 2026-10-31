#!/usr/bin/env python3
"""
Guard update_cooldown_modifiers against clobbering existing WCL talent ids.

cooldown_modifiers_{version}.json is hand-maintainable and accumulates mappings
over time; some entries were matched from an earlier export and stay correct even
once the current one no longer lists that talent. An earlier version of this
script assigned wcl_talent_ids unconditionally, so a spell missing from the export
had its working ids replaced with [] - silently losing WCL matches for talents
like Anger Management and Master of Time on every run.

Run:  python tools/db2_extractor/test_talent_mapping_preserves.py
"""

import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from build_talent_mapping import update_cooldown_modifiers


def write_modifiers(sections):
    tmp = Path(tempfile.mkdtemp()) / "cooldown_modifiers_test.json"
    data = {"metadata": {"note": "original note"}}
    data.update(sections)
    tmp.write_text(json.dumps(data, indent=2), encoding="utf-8")
    return tmp


def load(path):
    return json.loads(path.read_text(encoding="utf-8"))


def test_existing_ids_survive_a_miss():
    """A spell absent from the export keeps the ids it already had."""
    path = write_modifiers({
        "resource_modifiers": [
            {"talent_spell_id": 152278, "talent_name": "Anger Management",
             "wcl_talent_ids": [116851, 116884]},
        ],
    })
    update_cooldown_modifiers(path, {})  # empty export: nothing matches

    got = load(path)["resource_modifiers"][0]["wcl_talent_ids"]
    assert got == [116851, 116884], f"existing ids were clobbered: {got}"
    print("PASS: existing wcl_talent_ids survive a lookup miss")


def test_fresh_mapping_is_written():
    """A spell present in the export gets its ids filled in."""
    path = write_modifiers({
        "flat_modifiers": [
            {"talent_spell_id": 137040, "talent_name": "Elemental Shaman",
             "wcl_talent_ids": []},
        ],
    })
    update_cooldown_modifiers(path, {137040: [132239]})

    got = load(path)["flat_modifiers"][0]["wcl_talent_ids"]
    assert got == [132239], f"new mapping not written: {got}"
    print("PASS: a spell found in the export gets its ids written")


def test_export_wins_over_stale_ids():
    """When the export knows the spell, its answer replaces the old one."""
    path = write_modifiers({
        "flat_modifiers": [
            {"talent_spell_id": 342249, "talent_name": "Master of Time",
             "wcl_talent_ids": [11111]},
        ],
    })
    update_cooldown_modifiers(path, {342249: [80159]})

    got = load(path)["flat_modifiers"][0]["wcl_talent_ids"]
    assert got == [80159], f"export result did not win: {got}"
    print("PASS: a current export result replaces a stale id")


def test_unmapped_entry_gets_empty_list():
    """A spell with no ids and no match still ends up with the field present."""
    path = write_modifiers({
        "flat_modifiers": [
            {"talent_spell_id": 999999, "talent_name": "Removed Talent"},
        ],
    })
    update_cooldown_modifiers(path, {})

    assert load(path)["flat_modifiers"][0]["wcl_talent_ids"] == []
    print("PASS: an unmapped entry gets an empty wcl_talent_ids field")


def test_metadata_note_is_left_alone():
    """The hand-written note is not replaced by a generated one."""
    path = write_modifiers({"flat_modifiers": []})
    update_cooldown_modifiers(path, {})

    note = load(path)["metadata"]["note"]
    assert note == "original note", f"metadata note was overwritten: {note!r}"
    print("PASS: metadata note is preserved")


if __name__ == "__main__":
    test_existing_ids_survive_a_miss()
    test_fresh_mapping_is_written()
    test_export_wins_over_stale_ids()
    test_unmapped_entry_gets_empty_list()
    test_metadata_note_is_left_alone()
    print("\nAll talent-mapping preservation tests passed.")
