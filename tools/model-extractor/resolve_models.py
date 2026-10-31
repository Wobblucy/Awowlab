#!/usr/bin/env python3
"""
Resolve WoW enemies to the .m2 model fileDataIDs the model feature needs.

Two chains, both ending at a creaturemodeldata fileDataID (the .m2):

  boss:         journalencountercreature -> creaturedisplayinfo -> creaturemodeldata
  any-creature: creature[npcId]          -> creaturedisplayinfo -> creaturemodeldata

Bosses come from the curated DB2 encounter tables; M+ trash is not in those, so
it is resolved by npcId taken from a combat log's creature GUIDs. The two are
complementary - run both and merge.

Usage:
  # bosses for a set of journal instance ids
  python resolve_models.py --db2 <dir> --instances 1304,1309,...

  # every creature that appears in a combat log (M+ trash + bosses seen)
  python resolve_models.py --db2 <dir> --log "<combatlog.txt>"

  # both, merged
  python resolve_models.py --db2 <dir> --instances 1304,... --log "<log>"

Output: CSV on stdout (source,name,npc_or_encounter,display_id,model_id,m2_fdid)
Pass --out <file> to also write it.
"""

import argparse
import csv
import os
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from db2_layouts import find_export, verify_layout, DEFAULT_VERSION, resolve_db2_dir


def load_rows(db2_dir, table, version):
    """id -> raw field string, with the layout hash checked."""
    path = find_export(db2_dir, table)
    if path is None:
        raise FileNotFoundError(f"{table} export not found in {db2_dir}")
    verify_layout(path, table, version)
    rows = {}
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("Layout:") or ":" not in line:
                continue
            key, val = line.split(":", 1)
            try:
                rows[int(key)] = val.strip()
            except ValueError:
                pass
    return rows


def split_fields(s):
    """Comma split that keeps [a,b,c] arrays intact."""
    parts, depth, cur = [], 0, ""
    for ch in s:
        if ch == "[":
            depth += 1
            cur += ch
        elif ch == "]":
            depth -= 1
            cur += ch
        elif ch == "," and depth == 0:
            parts.append(cur)
            cur = ""
        else:
            cur += ch
    parts.append(cur)
    return parts


class ModelResolver:
    """Walks displayId -> modelId -> .m2 fileDataID, shared by both chains."""

    def __init__(self, db2_dir, version):
        self.cdi = load_rows(db2_dir, "creaturedisplayinfo", version)
        self.cmd = load_rows(db2_dir, "creaturemodeldata", version)

    def display_to_m2(self, display_id):
        """Return (model_id, fileDataID) for a creatureDisplayInfoID, or (None, None)."""
        row = self.cdi.get(display_id)
        if row is None:
            return None, None
        for tok in split_fields(row):
            tok = tok.strip()
            if not tok.isdigit():
                continue
            model_id = int(tok)
            model_row = self.cmd.get(model_id)
            if model_row is None:
                continue
            # creaturemodeldata layout: [geobox x6], Flags, FileDataID, ...
            # The .m2 fileDataID is the field right after the geobox array and
            # Flags - i.e. index 2. Do NOT just take the first int > 100000:
            # Flags can itself be a large value that is actually a BLP texture
            # fileDataID space collision, which yields a texture, not the model.
            fields = split_fields(model_row)
            if len(fields) > 2:
                fdid = fields[2].strip()
                if fdid.isdigit() and int(fdid) > 0:
                    return model_id, int(fdid)
        return None, None


def resolve_bosses(db2_dir, version, instances, resolver):
    """journalencountercreature rows whose encounter is in the target instances."""
    # journalencounter maps encounter -> instance; we filter creatures by it.
    je = load_rows(db2_dir, "journalencounter", version)
    enc_instance = {}
    for eid, row in je.items():
        b1, b2 = row.find("["), row.find("]", row.find("["))
        if b1 == -1 or b2 == -1:
            continue
        rest = row[b2 + 1:].strip().lstrip(",").split(",")
        if len(rest) >= 2 and rest[1].strip().isdigit():
            enc_instance[eid] = int(rest[1].strip())

    jec = load_rows(db2_dir, "journalencountercreature", version)
    out = []
    for _, row in jec.items():
        f = split_fields(row)
        name = f[0]
        # find the encounter id and the display id among the numeric fields
        enc = next((int(x) for x in f[1:] if x.strip().isdigit() and int(x) in enc_instance), None)
        if enc is None or enc_instance[enc] not in instances:
            continue
        disp = next((int(x) for x in f[1:] if x.strip().isdigit() and int(x) in resolver.cdi), None)
        if disp is None:
            continue
        model_id, fdid = resolver.display_to_m2(disp)
        if fdid:
            out.append(("boss", name, enc, disp, model_id, fdid))
    return out


CREATURE_GUID = re.compile(r'Creature-0-\d+-\d+-\d+-(\d+)-[0-9A-Fa-f]+,"([^"]+)"')


def npc_ids_from_log(log_path, max_bytes=256 * 1024 * 1024):
    """npcId -> name for every creature GUID seen in the log prefix."""
    npc = {}
    read = 0
    with open(log_path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            read += len(line)
            if read > max_bytes:
                break
            for m in CREATURE_GUID.finditer(line):
                npc.setdefault(int(m.group(1)), m.group(2))
    return npc


def resolve_creatures(db2_dir, version, npc_map, resolver):
    """creature[npcId] -> displayId array -> model, for npcIds seen in a log."""
    creature = load_rows(db2_dir, "creature", version)
    out = []
    for npc_id, name in sorted(npc_map.items()):
        row = creature.get(npc_id)
        if row is None:
            # player pets/summons and some spawns are absent from creature.txt
            out.append(("log-unresolved", name, npc_id, None, None, None))
            continue
        disp = None
        for tok in split_fields(row):
            tok = tok.strip()
            if tok.startswith("[") and tok.endswith("]"):
                ids = [int(x) for x in tok[1:-1].split(",") if x.strip().isdigit() and int(x) > 0]
                if ids and ids[0] in resolver.cdi:
                    disp = ids[0]
                    break
        if disp is None:
            out.append(("log-nodisplay", name, npc_id, None, None, None))
            continue
        model_id, fdid = resolver.display_to_m2(disp)
        out.append(("log", name, npc_id, disp, model_id, fdid))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--db2", help="DB2 text export directory "
                                  "(default: AWOWLAB_DB2_DIR, else rawdb2/<version>)")
    ap.add_argument("--version", default=DEFAULT_VERSION)
    ap.add_argument("--instances", help="comma-separated journal instance ids (bosses)")
    ap.add_argument("--log", help="combat log to pull creature npcIds from (M+ trash)")
    ap.add_argument("--out", help="also write the CSV here")
    args = ap.parse_args()

    if not args.instances and not args.log:
        ap.error("pass --instances, --log, or both")

    args.db2 = resolve_db2_dir(args.db2, args.version)
    resolver = ModelResolver(args.db2, args.version)
    rows = []

    if args.instances:
        instances = {int(x) for x in args.instances.split(",") if x.strip()}
        rows += resolve_bosses(args.db2, args.version, instances, resolver)

    if args.log:
        npc_map = npc_ids_from_log(args.log)
        print(f"# {len(npc_map)} distinct creatures in the log", file=sys.stderr)
        rows += resolve_creatures(args.db2, args.version, npc_map, resolver)

    # Dedupe by (name, fdid); keep resolved rows first.
    seen = set()
    deduped = []
    for r in sorted(rows, key=lambda x: (x[5] is None, x[1])):
        key = (r[1], r[5])
        if key in seen:
            continue
        seen.add(key)
        deduped.append(r)

    resolved = [r for r in deduped if r[5]]
    header = ["source", "name", "npc_or_encounter", "display_id", "model_id", "m2_fdid"]
    writer = csv.writer(sys.stdout)
    writer.writerow(header)
    for r in deduped:
        writer.writerow(r)

    print(f"\n# {len(resolved)} resolved to a model, {len(deduped) - len(resolved)} unresolved",
          file=sys.stderr)

    if args.out:
        with open(args.out, "w", encoding="utf-8", newline="") as f:
            w = csv.writer(f)
            w.writerow(header)
            w.writerows(deduped)
        # A bare fileDataID list for the extractor
        fdids = sorted({r[5] for r in resolved})
        fdid_path = Path(args.out).with_suffix(".fdids.txt")
        fdid_path.write_text("\n".join(str(x) for x in fdids) + "\n", encoding="utf-8")
        print(f"# wrote {args.out} and {fdid_path} ({len(fdids)} unique models)", file=sys.stderr)


if __name__ == "__main__":
    main()
