# Extracting Game Assets with CASC Explorer

How to pull map tiles and other art out of the WoW client when rolling AwowLab
forward to a new patch. Written after the 12.1 update, where most of the elapsed
time went to two problems that look like missing data but are not.

The two things that will waste your afternoon:

1. **A stale listfile hides whole folders.** CASC Explorer can only show files it
   has names for. A listfile published before the patch has no entry for the new
   season's art, so those folders simply do not appear — the data is there, the
   names are not.
2. **Retail and PTR share one install.** Both products live under a single
   directory with one shared CASC store. Opening the wrong one silently gives you
   the previous patch's art with no error.

## 1. Update the listfile

This is the step that makes new folders visible. Do it every patch.

Download the current community listfile:

    https://github.com/wowdev/wow-listfile

Take the `community-listfile.csv` asset from the latest release:

```bash
curl -L -o listfile.csv \
  https://github.com/wowdev/wow-listfile/releases/latest/download/community-listfile.csv
```

It is ~142 MB, formatted `fileDataID;path` one per line:

```
1;interface/cinematics/logo_800.avi
7775951;interface/worldmap/ulatek_raid/ulatek_raid_a1.blp
```

Replace `C:\CASCExplorer\listfile.csv` with it. That filename is what
`CASCExplorer.exe.config` points at (`ListFilePath` = `listfile.csv`), so keeping
the name means no configuration change. Back up the old one first if you want to
be able to compare.

**Check it actually covers the new content** before opening the tool — grep for
one of the new art-set names:

```bash
grep -ic "ulatek_raid" listfile.csv     # 12.1 raid; 0 means the listfile is too old
```

For scale: the listfile we started 12.1 with was seven months stale and returned
`0` for that set. The current one returns 2202.

## 2. Pick the right product

Products live side by side in one install root:

    C:\Program Files (x86)\World of Warcraft

Read the root `.build.info` (pipe-delimited) to see what each product is on:

```bash
python -c "
import csv
rows=list(csv.reader(open(r'C:\Program Files (x86)\World of Warcraft\.build.info',encoding='utf-8'),delimiter='|'))
hdr=[h.split('!')[0] for h in rows[0]]
[print('%-8s %s' % (d.get('Product'), d.get('Version'))) for d in (dict(zip(hdr,r)) for r in rows[1:] if r)]
"
```

During 12.1 that printed:

```
wow      12.0.7.68453     <- retail, a patch behind
wowt     12.1.0.68745     <- PTR, has the new season
```

`_retail_`, `_ptr_` and `_beta_` folders hold only executables — the data is in
the shared `Data\` store, so the folder you point at is the install root either
way. The **product** is what selects the build.

In CASC Explorer, open the local storage and choose the product by its label:
`wow` is "WoW Retail", `wowt` is "WoW Retail PTR". These names are listed in
`CASCExplorer.exe.config` under `OnlineStorageList`.

## 3. Find what you need

For map tiles, `tools/worldmap-builder/` derives the exact list from the DB2
data — you never have to guess filenames:

```bash
# uimap ids for the season, comma separated
python tools/worldmap-builder/list_required_files.py "2433,2500,2513,..."
```

That prints full virtual paths (`Interface\WorldMap\MurderRow\MurderRow_A1.blp`).
`season_12.1_tiles.csv` alongside it also carries the fileDataID, row and column
of every tile, which is useful when a name lookup fails but an ID lookup works.

## 4. Export

Navigate to the folder in the tree and export the files you need. Export as
**BLP** — `convert_tiles_to_png.py` handles the conversion, and keeping the BLPs
means you can re-convert without re-extracting.

Watch for **case-inconsistent folder names**. In 12.1, Altar of Fangs floors A
and B are under `Ulatek_Dungeon` while floor C is under `UlaTek_Dungeon` — a
capital T, a genuinely separate folder. Blizzard's inconsistency, easy to miss.

Exporting a whole folder is usually faster than picking individual files; the
downstream scripts ignore anything they do not need.

## 5. When a file will not extract

**Try the other tool.** During 12.1, rustydemon-cli 404'd on 87 of 252 tiles that
CASC Explorer read without complaint, from the same build, with a current
listfile. Merging both extracts is what completed the set. Two tools disagreeing
about one build means the tools differ, not the data.

Do not conclude an asset was never shipped until you have checked every extract
you have. We spent several cycles on that mistake — including running a client
repair twice — for files that were already sitting on disk from an earlier pull.

Things that sound like the cause but were not, in our case:

- **A partial install.** A Battle.net Scan and Repair that genuinely rewrote the
  PTR indices recovered exactly zero additional files.
- **Encryption.** `--tact-keys` with all 19,628 WoW keys from
  https://github.com/wowdev/TACTKeys changed nothing. A missing key reports
  *missing key* after locating the file; a `read failed` error is an earlier
  stage entirely, and keys will not help.
- **CASC format support.** A tool that could not handle the build would fail to
  open the storage, not extract two thirds of the files successfully.

## Dungeon geometry for encounter maps

Encounter-arena maps (`AwowLab/PNG/EncounterMaps/`) are top-down renders of the
dungeon's 3D geometry, exported from wow.export and rendered in Blender. Stock
wow.export runs out of JavaScript heap on the largest dungeons - the 12.1
Venomous Abyss raid (one WMO, 100+ groups) crashes it every time, and no amount
of free RAM helps because the ceiling is internal to NW.js. Use the patched
build from `tools/external/wow.export/`, which writes WMO geometry one group at
a time.

The whole flow, from export to the compressed images the app loads, is in
`tools/arena-maps/README.md`.

## What comes next

Extracted tiles feed the map pipeline. See
[UPDATE-FLOW.md](UPDATE-FLOW.md#map-images) for converting, stitching, and the
rule about not shipping partial maps — the stitcher will build one without
complaining and the result has blank holes.

## Reference

| What | Where |
|---|---|
| Community listfile | https://github.com/wowdev/wow-listfile |
| TACT keys | https://github.com/wowdev/TACTKeys |
| CASC Explorer | https://github.com/WoW-Tools/CASCExplorer |
| rustydemon (map tiles, stock release) | https://github.com/HoldMyBeer-gg/rustydemon |
| wow.export | https://github.com/Kruithne/wow.export, patched: `tools/external/wow.export/` |
| Default install root | `C:\Program Files (x86)\World of Warcraft` |
