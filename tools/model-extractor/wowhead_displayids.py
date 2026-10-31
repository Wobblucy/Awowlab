#!/usr/bin/env python3
"""
Resolve npcId -> creatureDisplayInfoID via wowhead, for dungeon trash whose
display link is not in the client DB2s (server-side runtime data).

Chain the app needs:  npcId -> [wowhead] -> displayId -> display_to_m2 -> .m2

Client DB2s cover bosses (journalencountercreature) and world creatures
(Creature/CreatureXDisplayInfo), but NOT instanced dungeon trash - the server
sends those displayIds at spawn time, so no client table holds npcId->display.
Wowhead datamines the runtime and exposes it on each npc page in g_pageInfo /
the ModelViewer displayId. This scrapes that one field.

Wowhead blocks plain HTTP (0-byte / bot detection), so this reads the rendered
page. Two modes:

  1. --browser-extract : print the JS snippet to run in a browser console/tool
     per npc (used by the automated in-app browser flow).
  2. --cache <file>    : a tab-separated npcId<TAB>displayId cache you populate
     (from the browser flow) that this merges + runs through display_to_m2.

Usage:
  # get the npcId list from a RAW combat log (real npcIds live in the GUIDs)
  python resolve_models.py --db2 <dir> --log "<WoWCombatLog.txt>" --out trash.csv
  # trash.csv 'log-unresolved' rows are the npcIds needing wowhead; feed them here:
  python wowhead_displayids.py --db2 <dir> --npcs 252565,252610,... --cache wh.tsv
"""
import argparse
import csv
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from db2_layouts import DEFAULT_VERSION  # noqa
sys.path.insert(0, str(Path(__file__).resolve().parent))
from resolve_models import ModelResolver  # noqa

# Wowhead blocks plain HTTP fetches (0-byte responses / bot detection), but an
# in-browser same-origin fetch loop works. Open ANY wowhead page once
# (https://www.wowhead.com/npc=<any id>), then run this in the browser console /
# an automation tool with your npcId list. It returns npcId<TAB>displayId lines;
# paste them into the --cache TSV. This is the whole scrape - one page load, one
# loop, no per-npc navigation. Regex-matching displayId in the page HTML is
# robust across wowhead layout changes.
BROWSER_JS = r"""
(async function(){
  const ids = [/* PASTE comma-separated npcIds here */];
  const out = [];
  for (const id of ids) {
    try {
      const r = await fetch(`https://www.wowhead.com/npc=${id}`, {credentials:'omit'});
      const html = await r.text();
      const m = html.match(/displayId["']?\s*[:=]\s*(\d+)/i);
      out.push(`${id}\t${m ? m[1] : 'NULL'}`);
    } catch(e) { out.push(`${id}\tERR`); }
    await new Promise(res => setTimeout(res, 150)); // be polite
  }
  return out.join('\n');
})();
"""


def load_cache(path):
    """npcId -> displayId from a TSV the browser step populates."""
    out = {}
    p = Path(path)
    if not p.exists():
        return out
    for line in p.read_text(encoding="utf-8").splitlines():
        parts = line.split("\t")
        if len(parts) >= 2 and parts[0].strip().isdigit() and parts[1].strip().isdigit():
            out[int(parts[0])] = int(parts[1])
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--db2", required=True)
    ap.add_argument("--version", default=DEFAULT_VERSION)
    ap.add_argument("--npcs", help="comma-separated npcIds to resolve")
    ap.add_argument("--npc-file", help="file with one npcId (optionally <tab>name) per line")
    ap.add_argument("--cache", help="npcId<TAB>displayId TSV from the browser step")
    ap.add_argument("--print-js", action="store_true", help="print the per-page browser JS and exit")
    ap.add_argument("--out", help="write the resolved CSV here")
    args = ap.parse_args()

    if args.print_js:
        print(BROWSER_JS)
        return

    npcs = {}
    if args.npcs:
        for x in args.npcs.split(","):
            x = x.strip()
            if x.isdigit():
                npcs[int(x)] = ""
    if args.npc_file:
        for line in Path(args.npc_file).read_text(encoding="utf-8").splitlines():
            parts = line.split("\t")
            if parts and parts[0].strip().isdigit():
                npcs[int(parts[0])] = parts[1] if len(parts) > 1 else ""

    if not npcs:
        ap.error("pass --npcs or --npc-file")

    cache = load_cache(args.cache) if args.cache else {}
    resolver = ModelResolver(args.db2, args.version)

    rows = []
    have = miss_display = miss_model = 0
    for npc, name in sorted(npcs.items()):
        disp = cache.get(npc)
        if disp is None:
            rows.append(("need-wowhead", name, npc, None, None, None))
            miss_display += 1
            continue
        model_id, fdid = resolver.display_to_m2(disp)
        if fdid:
            rows.append(("wowhead", name, npc, disp, model_id, fdid))
            have += 1
        else:
            rows.append(("display-no-model", name, npc, disp, None, None))
            miss_model += 1

    header = ["source", "name", "npc", "display_id", "model_id", "m2_fdid"]
    w = csv.writer(sys.stdout)
    w.writerow(header)
    for r in rows:
        w.writerow(r)

    print(f"\n# {have} resolved via wowhead, {miss_display} still need a wowhead "
          f"displayId, {miss_model} had a display but no model", file=sys.stderr)

    if args.out:
        with open(args.out, "w", encoding="utf-8", newline="") as f:
            wr = csv.writer(f)
            wr.writerow(header)
            wr.writerows(rows)
        fdids = sorted({r[5] for r in rows if r[5]})
        Path(args.out).with_suffix(".fdids.txt").write_text(
            "\n".join(map(str, fdids)) + "\n", encoding="utf-8")
        print(f"# wrote {args.out} (+ .fdids.txt, {len(fdids)} unique models)", file=sys.stderr)


if __name__ == "__main__":
    main()
