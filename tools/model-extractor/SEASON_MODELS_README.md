# Season Enemy Models

The app draws each dungeon creature with its real model, from one baked pack:
`AwowLab/data/season_models_12.1.awmp`. Building the pack takes three steps.

## 1. The key

Two files in this folder say which model each creature uses:

| File | What it is |
|------|------------|
| `season_model_fdids.txt` | Every `.m2` model to export, one fileDataID per line |
| `season_nameGroup_assignments.json` | Creature name to npcId and model (`model_<fdid>`) |

## 2. Export the models with wow.export

Set up the patched wow.export from `tools/external/wow.export/README.md`, then
export every fileDataID in the list as OBJ, named `model_<fdid>.obj`, into
`season_models/` in this folder. The headless script does the whole list in one
go:

```bash
export WOWEXPORT_MMAP_NODE="C:/path/to/wow.export/mmap.node"
node headless-extract.js "<wow-install>" <wow|wowt> \
    /path/to/AwowLab/tools/model-extractor/season_model_fdids.txt \
    /path/to/AwowLab/tools/model-extractor/season_models
```

Run it from the root of the patched wow.export checkout, with
`headless-extract.js` copied there from `tools/external/wow.export/scripts/`.
`wowt` is the PTR, `wow` is retail; use whichever client carries the patch you
are building for.

## 3. Bake the pack

```bash
python bake_season_models.py
```

Reads `season_models/` and the assignments, and writes
`AwowLab/data/season_models_12.1.awmp`. The bake decimates each model to an
actor-sized triangle budget, normalises it to a unit cube, and embeds the
npcId-to-model table, so the app loads it without parsing. `--budget`,
`--models`, `--assign` and `--out` override the defaults.

Rebaking the committed 12.1 exports reproduces the shipped pack byte for byte.
