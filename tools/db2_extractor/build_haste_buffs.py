#!/usr/bin/env python3
"""
Build the table of percentage haste buffs from the client data.

The haste tracker needs to know, for every aura a player can gain, how much haste
it grants, so that the GCD of each cast can be worked out from the log. Haste
rating from gear is already in COMBATANT_INFO; this covers the auras on top of it:
Bloodlust, Icy Veins, Berserking and the rest.

In the client data such a buff is a spelleffect row that applies aura type 193,
"modify all haste by a percentage", with the percentage in its base points. That
is the one aura type that moves the haste stat, and with it the global cooldown.
The neighbouring types only change part of it, and the GCD does not follow them:

    138, 140, 192, 319, 342   melee or ranged swing speed only
    65, 216                   cast time only

so they are left out.

Aura 193 appears on about 1900 spells, most of them creature abilities and old
test spells. A spell is kept only when a player can actually come by it:

  - it belongs to a player class's spell family (spellclassoptions), or
  - it is taught by a skill line - class, spec, race, profession (skilllineability), or
  - it is a talent (traitdefinition), or
  - an item casts it (itemeffect), or
  - a spell from one of those triggers it, directly or along a chain
    (spelleffect.EffectTriggerSpell)

Matching on names instead lets in about 300 creature spells called Bloodlust,
Enrage or Slow, so names are not used. A few player buffs are granted by a server
script, which leaves nothing in the client data tying them to the talent or
racial behind them. SCRIPT_GRANTED below lists those pairs; their values still
come from the data, and a pair that stops matching the data stops the run.

A buff whose base points are 0 gets its value from a script or another spell, so
the data alone cannot say what it is worth; it is left out and reported.

A handful of buffs keep their value in tenths of a percent, which only the
spell's tooltip text gives away (spell.Description and AuraDescription divide the
effect by 10); those are scaled to match.

Stacking comes from spellauraoptions: a buff that stacks grants its percentage
once per stack.

Input (converted DB2 text exports):
- spelleffect, spell, spellname, spellclassoptions, skilllineability,
  traitdefinition, itemeffect, spellauraoptions

Output:
- AwowLab/data/haste_buffs_{version}.json
"""

import argparse
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOLS_DIR))
sys.path.insert(0, str(TOOLS_DIR / "spell-data-generator"))
from db2_layouts import DEFAULT_VERSION, find_export, open_export, resolve_db2_dir  # noqa: E402
from extract_spell_gcd import parse_skilllineability  # noqa: E402

# Aura type: modify all haste by a percentage
HASTE_AURA = 193

# Effect that applies an aura to enemies in an area. Every other effect that
# carries an aura type puts it on the caster, their group or a friendly target.
AREA_AURA_ENEMY = 129

# "${$s1/10}" and the like in a tooltip: effect 1's value shown in tenths
TENTHS_RE = re.compile(r"\$\{\$[swm](\d)\s*/\s*10\}", re.IGNORECASE)

# spellclassoptions.SpellClassSet of each playable class
PLAYER_SPELL_FAMILIES = {
    3,    # Mage
    4,    # Warrior
    5,    # Warlock
    6,    # Priest
    7,    # Druid
    8,    # Rogue
    9,    # Hunter
    10,   # Paladin
    11,   # Shaman
    15,   # Death Knight
    53,   # Monk
    107,  # Demon Hunter
    224,  # Evoker
}

# Buff -> the player spell that grants it, for buffs a server script applies.
# Each was seen on players in cached reports and is otherwise unreachable.
SCRIPT_GRANTED = {
    343312: 343311,  # Furious Gaze, from the Demon Hunter talent
    202602: 202603,  # Into the Fray, from the Warrior talent
    154797: 154748,  # Touch of Elune - Night, from the Night Elf racial
}

# spelleffect columns. Arrays sit between the scalar fields and the spell id,
# so the scalars are counted from the left and the spell id taken from the end.
EFFECT_AURA = 0
EFFECT_DIFFICULTY = 1
EFFECT_INDEX = 2
EFFECT_TYPE = 3
EFFECT_TRIGGER_SPELL = 15
EFFECT_BASE_POINTS = 22


def read_rows(handle):
    """Yield (row_id, fields) from an export whose header is already consumed."""
    for line in handle:
        row_id, sep, rest = line.partition(":")
        if not sep or not rest.strip():
            continue
        try:
            yield int(row_id), rest.strip().split(",")
        except ValueError:
            continue


def parse_effects(db2_dir, version):
    """
    Return ({spell: [(effect index, base points)]}, {spell: set of triggered spells}).

    Only the base difficulty rows count. Rows for a particular raid or dungeon
    difficulty override creature abilities, not player buffs.
    """
    haste = defaultdict(list)
    triggers = defaultdict(set)
    with open_export(db2_dir, "spelleffect", version) as handle:
        for _, fields in read_rows(handle):
            if len(fields) <= EFFECT_BASE_POINTS:
                continue
            try:
                spell = int(fields[-1])
                trigger = int(fields[EFFECT_TRIGGER_SPELL])
                aura = int(fields[EFFECT_AURA])
                difficulty = int(fields[EFFECT_DIFFICULTY])
                index = int(fields[EFFECT_INDEX])
                effect = int(fields[EFFECT_TYPE])
                points = float(fields[EFFECT_BASE_POINTS])
            except ValueError:
                continue
            if trigger:
                triggers[spell].add(trigger)
            if aura == HASTE_AURA and difficulty == 0 and effect != AREA_AURA_ENEMY:
                haste[spell].append((index, points))
    return haste, triggers


def parse_tenths_effects(db2_dir, version):
    """
    {spell: effect indices whose value the spell's own text divides by 10}.

    Most haste effects hold whole percents, but a few hold tenths, and the only
    place that says so is the tooltip: Alacrity reads "Haste increased by
    ${$s1/10}.1%" over a base of 15. The text fields hold commas, so the row is
    searched whole rather than split.
    """
    scaled = defaultdict(set)
    with open_export(db2_dir, "spell", version) as handle:
        for line in handle:
            row_id, sep, text = line.partition(":")
            if not sep or "/10" not in text:
                continue
            try:
                spell = int(row_id)
            except ValueError:
                continue
            for match in TENTHS_RE.finditer(text):
                scaled[spell].add(int(match.group(1)) - 1)
    return scaled


def haste_percent(effects, tenths):
    """Total haste percent of one spell's haste effects."""
    return sum(points / 10 if index in tenths else points for index, points in effects)


def parse_spell_names(db2_dir, version):
    names = {}
    with open_export(db2_dir, "spellname", version) as handle:
        for line in handle:
            row_id, sep, name = line.partition(":")
            if sep:
                try:
                    names[int(row_id)] = name.strip()
                except ValueError:
                    continue
    return names


def parse_class_spells(db2_dir, version):
    """Spells whose spell family is a playable class's."""
    spells = set()
    with open_export(db2_dir, "spellclassoptions", version) as handle:
        # SpellID, ModalNextSpell, SpellClassSet, SpellClassMask[4]
        for _, fields in read_rows(handle):
            try:
                if int(fields[2]) in PLAYER_SPELL_FAMILIES:
                    spells.add(int(fields[0]))
            except (ValueError, IndexError):
                continue
    return spells


def parse_skill_spells(db2_dir, version):
    """Every spell a skill line teaches."""
    path = find_export(db2_dir, "skilllineability")
    if path is None:
        raise SystemExit(f"no skilllineability export in {db2_dir}")
    # The column map is keyed by layout hash, so this also fails on a shape it
    # has not seen. The recorded hash is checked as well, for the version asked.
    open_export(db2_dir, "skilllineability", version).close()
    spells = set()
    for taught in parse_skilllineability(path).values():
        spells |= taught
    return spells


def parse_talent_spells(db2_dir, version):
    """Spells a talent grants, replaces or shows."""
    spells = set()
    with open_export(db2_dir, "traitdefinition", version) as handle:
        # The three leading text fields can contain commas, so the numeric
        # columns are read from the end:
        # ..., ID, SpellID, OverrideIcon, OverridesSpellID, VisibleSpellID
        for _, fields in read_rows(handle):
            if len(fields) < 5:
                continue
            for value in (fields[-4], fields[-2], fields[-1]):
                try:
                    if int(value):
                        spells.add(int(value))
                except ValueError:
                    continue
    return spells


def parse_item_spells(db2_dir, version):
    """Spells an item casts on use, on equip or on a proc."""
    spells = set()
    with open_export(db2_dir, "itemeffect", version) as handle:
        # LegacySlotIndex, TriggerType, Charges, CoolDownMSec,
        # CategoryCoolDownMSec, SpellCategoryID, SpellID, ...
        for _, fields in read_rows(handle):
            try:
                if int(fields[6]):
                    spells.add(int(fields[6]))
            except (ValueError, IndexError):
                continue
    return spells


def parse_max_stacks(db2_dir, version):
    """{spell: most stacks the aura can reach}, for spells that stack."""
    stacks = {}
    with open_export(db2_dir, "spellauraoptions", version) as handle:
        # DifficultyID, CumulativeAura, ProcCategoryRecovery, ProcChance,
        # ProcCharges, SpellProcsPerMinuteID, ProcTypeMask[2], SpellID
        for _, fields in read_rows(handle):
            try:
                if int(fields[0]) != 0:
                    continue
                cumulative = int(fields[1])
                if cumulative > 1:
                    stacks[int(fields[-1])] = cumulative
            except (ValueError, IndexError):
                continue
    return stacks


def reachable(roots, triggers):
    """Every spell in roots plus every spell they trigger, transitively."""
    seen = set(roots)
    pending = list(roots)
    while pending:
        for child in triggers.get(pending.pop(), ()):
            if child not in seen:
                seen.add(child)
                pending.append(child)
    return seen


def build_table(db2_dir, version, script_granted=SCRIPT_GRANTED):
    """
    Return (buffs, no_value, aura_spell_count).

    buffs maps spell id -> {"haste": permille per stack, "stacks": max stacks,
    "name": ...}; no_value lists the (spell, name) pairs left out because their
    base points are 0.
    """
    effects, triggers = parse_effects(db2_dir, version)
    tenths = parse_tenths_effects(db2_dir, version)
    haste = {spell: haste_percent(rows, tenths.get(spell, ()))
             for spell, rows in effects.items()}
    names = parse_spell_names(db2_dir, version)
    roots = (parse_class_spells(db2_dir, version)
             | parse_skill_spells(db2_dir, version)
             | parse_talent_spells(db2_dir, version)
             | parse_item_spells(db2_dir, version))
    player_spells = reachable(roots, triggers)

    for buff, source in script_granted.items():
        if source not in roots or round(haste.get(buff, 0) * 10) == 0:
            raise SystemExit(
                f"SCRIPT_GRANTED pair {buff} <- {source} no longer matches the data: "
                f"{source} is {'' if source in roots else 'not '}a player spell, "
                f"{buff} grants {haste.get(buff, 0)}% haste. Check both spells and "
                f"update or drop the pair.")
        player_spells.add(buff)

    max_stacks = parse_max_stacks(db2_dir, version)

    buffs = {}
    no_value = []
    for spell, percent in sorted(haste.items()):
        if spell not in player_spells:
            continue
        name = names.get(spell, "")
        # Client test rows are marked do-not-translate
        if "[DNT]" in name:
            continue
        permille = round(percent * 10)
        if permille == 0:
            no_value.append((spell, name))
            continue
        buffs[spell] = {"haste": permille, "stacks": max_stacks.get(spell, 1), "name": name}
    return buffs, no_value, len(haste)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("db2_dir", type=Path, nargs="?", default=None,
                        help="directory holding the converted DB2 text exports "
                             "(default: AWOWLAB_DB2_DIR, else rawdb2/<version>)")
    parser.add_argument("--version", default=DEFAULT_VERSION)
    parser.add_argument("--output", type=Path, default=None)
    args = parser.parse_args()

    db2_dir = resolve_db2_dir(args.db2_dir, args.version)
    buffs, no_value, aura_spells = build_table(db2_dir, args.version)

    print(f"spells applying aura {HASTE_AURA}: {aura_spells}")
    print(f"  a player can gain: {len(buffs) + len(no_value)}")
    print(f"  written: {len(buffs)} ({sum(1 for b in buffs.values() if b['stacks'] > 1)} stacking)")
    if no_value:
        print(f"  left out, value set outside the spell data ({len(no_value)}):")
        for spell, name in no_value:
            print(f"    {spell} {name}")

    output = args.output or (TOOLS_DIR.parent / "AwowLab" / "data"
                             / f"haste_buffs_{args.version}.json")
    output.parent.mkdir(parents=True, exist_ok=True)

    # One buff per line, so a patch's changes read as a short diff
    lines = [f'    "{spell}": {json.dumps(info, ensure_ascii=False)}'
             for spell, info in buffs.items()]
    comment = ("Percentage haste buffs: spell id -> haste in tenths of a percent "
               "per stack, and the most stacks the aura reaches. Generated from "
               "the client data by tools/db2_extractor/build_haste_buffs.py; "
               "regenerate after a patch rather than editing by hand.")
    with open(output, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("{\n")
        handle.write(f'  "version": {json.dumps(args.version)},\n')
        handle.write(f'  "_comment": {json.dumps(comment)},\n')
        handle.write('  "buffs": {\n')
        handle.write(",\n".join(lines))
        handle.write("\n  }\n}\n")

    print(f"wrote {output}")


if __name__ == "__main__":
    main()
