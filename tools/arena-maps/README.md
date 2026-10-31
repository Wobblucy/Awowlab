# Arena Maps

Top-down images of each boss arena, shown under the replay. The app finds them
in `AwowLab/PNG/EncounterMaps/` by file name:

```
encounter_{encounterId}_x{X}_y{Y}_s{scale}.png
```

`X`, `Y` are the camera position and `scale` its orthographic scale, all in the
Blender scene's coordinates. The app reads them back from the name to line
combat-log positions up with the picture
(`UIManager/src/UI/EncounterTextureManager.cpp`), so never rename a rendered
file.

## What you need

- Blender 4.2 or later (5.0 used for 12.1), with the patched `io_scene_wowobj`
  addon installed - see `tools/external/wow.export/README.md`
- A patched wow.export - see `tools/external/wow.export/`
- Python with Pillow, for the contact sheets
- pngquant, for the last step

## Steps

### 1. Export the dungeon geometry

In the patched wow.export, open the dungeon's root WMO in the model viewer and
export it as OBJ. Keep the `.json` written beside the `.obj`; step 3 needs it.
Large raids crash unpatched wow.export.

### 2. Build the scene

In Blender, *File > Import > WoW Object (.obj)* and pick the exported OBJ. When
a `<name>_ModelPlacementInformation.csv` sits beside it, the addon reads it and
places each model in the world. Save the scene as a `.blend`; everything after
this runs headless against that file.

### 3. Add the liquid (WMO exports only)

The importer skips liquid stored inside a WMO, so pools and acid floors come
out dry. This builds them from the export's JSON and saves the scene:

```bash
blender -b scene.blend --python import_wmo_liquid.py -- <export>.json <imported object name>
```

The object name is the OBJ's file name as Blender shows it, for example
`12du_ulatek_raid01_set0.obj`.

### 4. Get the boss positions

```bash
node headless-encounters.js "<wow-install>" <wow|wowt> <mapId> encounters.json
```

(`tools/external/wow.export/scripts/`, run from the patched wow.export
checkout.) This writes every journal encounter for the map with its world
position. It has no height, which is what the next step is for.

### 5. Pick the framing

A first pass from the journal positions alone:

```bash
blender -b scene.blend --python render_arenas.py -- first_pass --encounters encounters.json
```

Rooms in these dungeons are stacked, so a boss position alone often frames the
roof or the room below. For each arena that comes out wrong, sweep it and look:

```bash
blender -b scene.blend --python sweep_arena.py -- sweep \
    --id 3455 --at -232 -477 --scales 240 300 --floors 438 466 518 557
python contact_sheet.py sweep -o sweep.png
```

`--floors` tries candidate storeys; `--camera-z` sets camera heights directly
for open rooms where no floor and ceiling pair gives a clean view. The floor
heights printed by the first pass are a good place to start.

Write the chosen values into `framing/<dungeon>.json`, one entry per encounter:
`x`, `y`, `scale`, and either `floor` or `camera_z`. Add `"liquid": false` for
an arena where the liquid from step 3 renders badly; Sszorak's ring, for one,
comes out covered in white sheets. See `framing/venomous_abyss_12.1.json`.

### 6. Render

```bash
blender -b scene.blend --python render_arenas.py -- renders \
    --framing framing/<dungeon>.json --encounters encounters.json
```

Encounters in the framing file use the picked values; any others get a
first-pass render from their journal position.

### 7. Compress into the app

```bash
python tools/encounter-map-compressor/compress_encounter_maps.py renders
```

This quantises each image with pngquant and copies it, name unchanged, into
`AwowLab/PNG/EncounterMaps/`.

## Files

| File | Runs in | Does |
|------|---------|------|
| `arena_scene.py` | Blender | Lighting, liquid and camera setup shared by the render and sweep |
| `render_arenas.py` | Blender | Renders every arena, named for the app |
| `sweep_arena.py` | Blender | Renders one arena at several scales and heights |
| `contact_sheet.py` | Python | Lays sweep shots out side by side |
| `import_wmo_liquid.py` | Blender | Builds WMO liquid from a wow.export JSON and saves the scene |
| `framing/*.json` | - | Picked framing per dungeon |
