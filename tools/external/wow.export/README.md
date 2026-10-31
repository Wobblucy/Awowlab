# wow.export

Upstream: <https://github.com/Kruithne/wow.export> (MIT)
Base commit: `c2fd7bde36a712be78a5da896c995b84fbfa2545` (0.2.19, still upstream
`main` as of 2026-10-31)

We use wow.export to export creature models, export dungeon geometry for the
encounter maps, and import that geometry into Blender with its bundled
`io_scene_wowobj` addon. Large raid exports crash stock 0.2.19 and some textures
import as black, so we run it with the patches below.

## Patches

Apply the folders in order; each one assumes the ones before it.

### 1. `1-pr590-wmo-export-oom/` - upstream PR [#590](https://github.com/Kruithne/wow.export/pull/590), open

Exporting a large WMO (the 12.1 Venomous Abyss raid, 100+ groups) runs out of
JavaScript heap and the app closes.

1. Use typed arrays instead of spread-concatenated plain arrays for geometry.
2. Write OBJ geometry one group at a time instead of merging every group first.
3. Evict groups from the loader cache once exported, so map exports that revisit
   the same WMO from several tiles do not accumulate them.
4. Keep vertex colour lines aligned when some groups have no colours (the
   Blender addon pairs them with vertices by order).

### 2. `2-pr591-texture-decode-dedupe/` - upstream PR [#591](https://github.com/Kruithne/wow.export/pull/591), open

The texture phase of the same raid export decoded 28,000+ textures in about an
hour. These patches decode each distinct texture once (423 decodes, about 90
seconds) with identical output.

1. Cache decoded textures during a WMO export.
2. Key that cache on destination path too, so a texture shared between two WMOs
   is still written into each one's folder.

### 3. `3-unsubmitted/` - not sent upstream

1. Release GL buffers when baking ADT terrain tiles. Large map exports leaked
   enough buffers to leave later tiles baking blank.
2. Seed terrain vertex colours with a neutral value, and log bake inputs.
3. Normalise single-entry ADT texture id lists to arrays, which fixed tiles
   baking solid black.
4. Set the alpha mode on the Blender addon's shader 20 path so those textures
   do not import as black.
5. Export `get_encounter_world_positions` from `journal-encounters.js`, which
   `scripts/headless-encounters.js` calls. Apply this one with `git apply`
   rather than `git am`; it is a plain diff, not a commit.

### 4. `4-pr594-blender-channel-packed/` - upstream PR [#594](https://github.com/Kruithne/wow.export/pull/594), open

Many opaque WoW textures ship with an unused alpha channel of all zeros. Blender
multiplies colour by that alpha, so the surfaces import as black. This loads
every texture as channel-packed in the addon, which covers the shader 20 case
from folder 3 and every other material path.

## Building

```bash
git clone https://github.com/Kruithne/wow.export && cd wow.export
git config core.autocrlf false
git checkout c2fd7bde36a712be78a5da896c995b84fbfa2545
P=/path/to/AwowLab/tools/external/wow.export/patches
git am --keep-cr $P/1-*/*.patch $P/2-*/*.patch
git am --keep-cr $P/3-unsubmitted/000[1-4]*.patch
git apply $P/3-unsubmitted/0005-*.patch
git am --keep-cr $P/4-*/*.patch
```

Then build with bun as upstream's README describes. If you already have an
installed wow.export and only need the patched application code, copy
`scripts/build-bundle-only.js` into the repository root and run
`bun build-bundle-only.js`. That writes just the `app.js` bundle to
`bin/bundle-only/`, which replaces `src/app.js` in the installed app.

### Blender addon

Blender loads addons from its own folder, not from wow.export. After patching,
copy `addons/io_scene_wowobj/` to
`%APPDATA%\Blender Foundation\Blender\<version>\scripts\addons\` for each
Blender version you use, delete `__pycache__` there, and restart Blender.

## Scripts

These run wow.export's own code without its window. Copy them into the root of
the patched wow.export checkout, install its dependencies once with
`npm install`, and run them with node (Node.js 18 or later). bun cannot load
wow.export's CASC modules, so these fail under bun even though the app itself
builds with it. Both need the native `mmap.node` addon from an installed copy of
wow.export, given by environment variable:

```bash
export WOWEXPORT_MMAP_NODE="C:/path/to/wow.export/mmap.node"
```

| Script | Does | Usage |
|--------|------|-------|
| `headless-extract.js` | Exports a list of `.m2` models as OBJ. Used for the season enemy models, see `tools/model-extractor/SEASON_MODELS_README.md` | `node headless-extract.js <wow-install> <product> <fdid-list.txt> <out-dir>` |
| `headless-encounters.js` | Writes journal encounter world positions for a map as JSON, for framing encounter-map renders. Needs patch 3.5 | `node headless-encounters.js <wow-install> <product> <map-id> <out.json>` |
| `build-bundle-only.js` | Builds only the `app.js` bundle, as above (uses bun's bundler, so this one needs bun) | `bun build-bundle-only.js` |

`<product>` is `wow` for retail or `wowt` for the PTR.
