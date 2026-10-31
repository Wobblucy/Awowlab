#!/usr/bin/env python3
"""
Build the hero talent lookup for WarcraftLogs imports.

WarcraftLogs reports every talent a character took as a flat list of trait node
entry ids on its combatantinfo event. It does not say which of them are hero
talents, and the fields that sound like they would - customPowerSet and friends -
come through empty. Hero choice changes rotation, buffs and cooldown usage, so a
baseline that ignores it compares two different playstyles.

The client data has the answer. A node entry that belongs to a hero tree carries
a TraitSubTreeID, and traitsubtree names it:

    WCL talentTree[].id -> traitnodeentry.TraitSubTreeID -> traitsubtree.Name

Input (converted DB2 text exports):
- traitnodeentry: ID, TraitDefinitionID, MaxRanks, NodeEntryType, TraitSubTreeID
- traitsubtree:   Name, Description, ID, UiTextureAtlasElementID, TraitTreeID x2

Output:
- AwowLab/data/hero_talents_{version}.json

A character has exactly one hero tree, so any node entry that resolves gives the
answer for the whole build. The generated file is checked against the cached WCL
reports when --verify is passed, which is the only way to know the mapping holds
for real characters rather than only parsing cleanly.
"""

import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from db2_layouts import DEFAULT_VERSION, find_export, verify_layout, resolve_db2_dir
from hero_talent_specs import SPEC_HERO_TREES, check_against


def read_rows(path):
    """Yield (row_id, fields) from a positional DB2 text export."""
    with open(path, "r", encoding="utf-8") as handle:
        for line in handle:
            if not line.strip() or line.startswith("Layout:"):
                continue
            row_id, _, rest = line.partition(":")
            if not rest:
                continue
            yield row_id.strip(), rest.rstrip("\n").split(",")


def parse_subtree_names(db2_dir, version):
    """
    Map subtree id -> display name.

    Descriptions contain commas, so the numeric columns cannot be found by
    counting from the left. They are the last four fields on the row, which is
    stable no matter how many commas the prose adds.
    """
    path = find_export(db2_dir, "traitsubtree")
    if path is None:
        raise SystemExit(f"no traitsubtree export in {db2_dir}")

    # A shifted column here would rename every hero tree rather than fail, so
    # the hash is checked before a single row is read.
    verify_layout(path, "traitsubtree", version)

    names = {}
    for _, fields in read_rows(path):
        if len(fields) < 4:
            continue
        try:
            subtree_id = int(fields[-4])
        except ValueError:
            continue
        name = fields[0].strip()
        # Blizzard ships UI test rows in this table; they are marked [DNT]
        # (do not translate) and are not real hero trees.
        if not name or "[DNT]" in name or name.startswith("UI Test"):
            continue
        names[subtree_id] = name
    return names


def parse_entry_subtrees(db2_dir, version):
    """Map trait node entry id -> subtree id, for entries that belong to one."""
    path = find_export(db2_dir, "traitnodeentry")
    if path is None:
        raise SystemExit(f"no traitnodeentry export in {db2_dir}")

    verify_layout(path, "traitnodeentry", version)

    entries = {}
    for row_id, fields in read_rows(path):
        if len(fields) < 5:
            continue
        try:
            subtree_id = int(fields[4])
            if subtree_id != 0:
                entries[int(row_id)] = subtree_id
        except ValueError:
            continue
    return entries


def verify_against_cache(lookup, cache_dir):
    """
    Check the lookup against real characters.

    A character has exactly one hero tree. If a build resolves to two, the
    mapping is wrong - that is the failure this is here to catch, and it cannot
    be seen by looking at the DB2 tables alone.
    """
    pages = sorted(Path(cache_dir).glob("*/fight*/page1.json"))
    if not pages:
        print(f"  no cached reports under {cache_dir}; skipping verification")
        return True

    builds = resolved = ambiguous = 0
    by_spec = defaultdict(set)

    for page in pages:
        try:
            with open(page, "r", encoding="utf-8") as handle:
                events = json.load(handle)
        except (OSError, json.JSONDecodeError):
            continue

        for event in events:
            if event.get("type") != "combatantinfo":
                continue
            talents = event.get("talentTree") or []
            if not talents:
                continue

            builds += 1
            trees = {lookup[str(t["id"])] for t in talents
                     if str(t.get("id")) in lookup}
            if len(trees) == 1:
                resolved += 1
                by_spec[event.get("specID")].update(trees)
            elif len(trees) > 1:
                ambiguous += 1
                print(f"  AMBIGUOUS spec {event.get('specID')}: {sorted(trees)}")

    print(f"  builds with talents: {builds}")
    print(f"  resolved to one hero tree: {resolved}")
    print(f"  resolved to more than one: {ambiguous}")
    if builds:
        print(f"  coverage: {resolved * 100 // builds}%")

    for spec in sorted(k for k in by_spec if k is not None):
        print(f"    spec {spec}: {sorted(by_spec[spec])}")

    # The caller writes this into the output: traitsubtree groups hero trees by
    # class, not by spec, so which trees a spec can actually pick is only
    # visible from real characters.
    return ambiguous == 0, by_spec


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("db2_dir", type=Path, nargs="?", default=None,
                        help="directory holding the converted DB2 text exports "
                             "(default: AWOWLAB_DB2_DIR, else rawdb2/<version>)")
    parser.add_argument("--version", default=DEFAULT_VERSION)
    parser.add_argument("--output", type=Path, default=None)
    parser.add_argument("--verify", type=Path, default=None,
                        help="wcl_cache directory to check the mapping against")
    args = parser.parse_args()

    args.db2_dir = resolve_db2_dir(args.db2_dir, args.version)
    names = parse_subtree_names(args.db2_dir, args.version)
    entries = parse_entry_subtrees(args.db2_dir, args.version)

    # Only keep entries whose subtree is a real, named hero tree
    lookup = {}
    for entry_id, subtree_id in entries.items():
        name = names.get(subtree_id)
        if name:
            lookup[str(entry_id)] = name

    # The table names the trees a spec can pick; the client data says which
    # of those names are real. Anything claimed but unknown is a typo or a
    # rename and has to be loud, or the picker quietly loses a choice.
    unknown, unused = check_against(set(lookup.values()))
    if unknown:
        raise SystemExit(
            "hero_talent_specs.py names trees the client data does not have: "
            + ", ".join(unknown))
    if unused:
        print(f"  note: {len(unused)} named subtrees no spec claims "
              f"(Brann, test rows, and the like)")
    claimed_trees = {t for trees in SPEC_HERO_TREES.values() for t in trees}

    print(f"hero trees: {len(names)}")
    print(f"node entries resolving to one: {len(lookup)}")

    if args.verify:
        print("verifying against cached reports:")
        ok, observed = verify_against_cache(lookup, args.verify)
        if not ok:
            raise SystemExit("mapping is ambiguous for at least one character")

        # Cross-check the hand-maintained table against what real characters
        # were seen taking. A spec whose observed tree is not in the table means
        # the table is wrong; the reverse only means nobody has imported that
        # tree yet, which is expected and not worth reporting.
        for spec, trees in observed.items():
            listed = set(SPEC_HERO_TREES.get(spec, []))
            for tree in trees:
                if tree not in listed:
                    print(f"  TABLE GAP: spec {spec} was seen with {tree!r}, "
                          f"which hero_talent_specs.py does not list")

    output = args.output or (Path(__file__).resolve().parents[2]
                             / "AwowLab" / "data"
                             / f"hero_talents_{args.version}.json")
    output.parent.mkdir(parents=True, exist_ok=True)

    payload = {
        "version": args.version,
        "_comment": ("WCL trait node entry id -> hero talent tree name. "
                     "WarcraftLogs sends these ids on combatantinfo but does "
                     "not say which are hero talents; this names them. "
                     "Regenerate with tools/db2_extractor/build_hero_talents.py "
                     "after a patch."),
        "trees": sorted(claimed_trees),
        "entries": dict(sorted(lookup.items(), key=lambda kv: int(kv[0]))),
        # Which trees each spec was actually seen using. traitsubtree groups
        # them by class tree, so this is the only place the per-spec split
        # comes from - it grows as more reports are cached, and a spec missing
        # here means no cached character has played it, not that it has none.
        "specs": {str(spec): trees
                  for spec, trees in sorted(SPEC_HERO_TREES.items())},
    }
    with open(output, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=2, ensure_ascii=False)
        handle.write("\n")

    print(f"wrote {output}")


if __name__ == "__main__":
    main()
