#!/usr/bin/env python3
"""
Check build_haste_buffs.py's selection rules on a tiny hand-made export.

Each spell below stands for one rule: a class spell is kept, a creature spell
with the same aura is not, a talent's triggered buff is kept, a value the
tooltip shows in tenths is scaled, an item buff carries its stack count, and a
buff with no value in the data is reported rather than written as 0.

Run:  python tools/db2_extractor/test_haste_buffs.py
"""

import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from build_haste_buffs import build_table  # noqa: E402
from db2_layouts import expected_hash  # noqa: E402

VERSION = "12.1"


def effect_row(spell, aura=0, points=0, trigger=0, index=0, effect=6):
    scalars = [0] * 25
    scalars[0], scalars[2], scalars[3] = aura, index, effect
    scalars[15], scalars[22] = trigger, points
    return ",".join(map(str, scalars)) + f",[0,0],[0,0],[0,0,0,0],[1,0],{spell}"


def write_exports(folder):
    tables = {
        "spelleffect": [
            effect_row(100, aura=193, points=30),             # class buff
            effect_row(101, aura=193, points=50),             # creature buff
            effect_row(200, trigger=102),                     # talent triggers 102
            effect_row(102, aura=193, points=15),             # value in tenths
            effect_row(103, aura=193, points=5),              # item buff, stacks
            effect_row(104, aura=193, points=0),              # value set elsewhere
            effect_row(105, aura=193, points=-20, effect=129),  # enemy area aura
        ],
        "spell": [",,", ",Haste increased by ${$s1/10}.1%.,", ",,", ",,", ",,", ",,"],
        "spellname": ["A", "B", "C", "D", "E", "F", "G"],
        "spellclassoptions": ["100,0,3,[0,0,0,0]", "104,0,3,[0,0,0,0]",
                              "105,0,3,[0,0,0,0]", "101,0,0,[0,0,0,0]"],
        "skilllineability": [],
        "traitdefinition": [",,,1,200,0,0,0"],
        "itemeffect": ["0,0,0,0,0,0,103,0,0"],
        "spellauraoptions": ["0,3,0,100,0,0,[0,0],103"],
    }
    ids = {
        "spell": [99, 102, 100, 101, 103, 104],
        "spellname": [100, 101, 102, 103, 104, 105, 200],
    }
    for name, rows in tables.items():
        lines = [f"Layout: {expected_hash(name, VERSION)}"]
        row_ids = ids.get(name, range(1, len(rows) + 1))
        lines += [f"{row_id}: {row}" for row_id, row in zip(row_ids, rows)]
        (folder / f"{name}.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")


def test_selection_rules():
    folder = Path(tempfile.mkdtemp())
    write_exports(folder)
    buffs, no_value, _ = build_table(folder, VERSION, script_granted={})

    assert buffs[100]["haste"] == 300 and buffs[100]["stacks"] == 1, buffs.get(100)
    print("PASS: a class spell's haste aura is kept, as permille")

    assert 101 not in buffs, "a creature spell was kept"
    print("PASS: a creature spell with the same aura is left out")

    assert buffs[102]["haste"] == 15, buffs.get(102)
    print("PASS: a talent's triggered buff is kept, scaled by its tooltip's /10")

    assert buffs[103] == {"haste": 50, "stacks": 3, "name": "D"}, buffs.get(103)
    print("PASS: an item buff is kept with its stack count")

    assert 104 not in buffs and (104, "E") in no_value
    print("PASS: a buff with no value in the data is reported, not written")

    assert 105 not in buffs, "an aura put on enemies was kept"
    print("PASS: an aura applied to enemies is left out")


if __name__ == "__main__":
    test_selection_rules()
    print("\nAll haste buff tests passed.")
