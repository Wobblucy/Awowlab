# How to Build WorldMaps from BLP Tiles

This document explains how WoW WorldMap IDs (like 2460-2471 for Manaforge Omega) connect to the individual .blp texture tiles.

## Regenerating map_world_bounds.csv (start here)

`map_world_bounds.csv` is the runtime world-bounds table. The app loads it on
startup; if a map's `mapId` is not in this file the floor renders as a blank
grid and the map texture is never loaded. The CSV ships at the canonical source
path `AwowLab/PNG/Worldmaps/map_world_bounds.csv`, which is what the release
packages from.

It is generated from the DB2 exports in two stages, both run from this folder:

```
DB2 .txt exports
  -> generate_master_coordinates.py <db2_folder>   # -> master_map_alignment_coordinates.json
  -> extract_map_bounds.py                          # -> AwowLab/PNG/Worldmaps/map_world_bounds.csv
```

1. **DB2 -> JSON.** Run `generate_master_coordinates.py`, pointing it at the
   folder of DB2 `.txt` exports. Without an argument it reads `AWOWLAB_DB2_DIR`,
   then prompts with `rawdb2/<version>` as the default:

   ```bash
   python generate_master_coordinates.py "<exports>"
   ```

   This writes `master_map_alignment_coordinates.json` (every map in the DB2).

2. **JSON -> CSV.** Run `extract_map_bounds.py` (no arguments). It reads the JSON
   and writes the CSV **directly to the canonical source path** -- there is no
   separate copy step:

   ```bash
   python extract_map_bounds.py
   ```

The CSV intentionally contains bounds for every map in the DB2, not just the
ones we ship textures for. Extra rows are harmless (a map with no PNG simply
never loads a texture), and a map already has bounds the moment its PNG is
added. Required columns: `mapId,minX,minY,maxX,maxY,imageWidth,imageHeight`.

The rest of this document covers how the underlying DB2 tables relate, for
building or troubleshooting the map textures themselves.

## The Relationship Chain

```
UIMap ID → UiMapXMapArt → UiMapArt ID → UiMapArtTile → FileDataID → Manifest → .blp filename
```

## DB2 Tables Involved

| Table | Purpose |
|-------|---------|
| `uimap` | Defines map IDs and names (e.g., 2465 = "Manaforge Omega") |
| `uimapxmapart` | Links UIMap IDs to UiMapArt IDs |
| `uimaparttile` | Defines tile grid positions and FileDataIDs for each UiMapArt |
| `manifestinterfacedata` | Maps FileDataIDs to actual .blp file paths |

## Step-by-Step Process

### Step 1: UIMap to UiMapArt Mapping

From `uimapxmapart_export.txt` (format: `ID: FloorIndex, UiMapArtID, UiMapID`):

```powershell
# Find UiMapArt ID for a specific UIMap
Select-String -Path "extracted db2\uimapxmapart_export.txt" -Pattern ",YOUR_UIMAP_ID$"
```

### Step 2: UiMapArt to Tiles

From `uimaparttile_export.txt` (format: `ID: Row, Column, LayerIndex, FileDataID, UiMapArtID`):

```powershell
# Find all tiles for a UiMapArt ID
Select-String -Path "extracted db2\uimaparttile_export.txt" -Pattern ",YOUR_UIMAPART_ID$"
```

Example output:
```
95051: 0,0,0,6919238,1975   -> Row 0, Col 0, Layer 0, FileDataID 6919238
95052: 0,1,0,6919242,1975   -> Row 0, Col 1, Layer 0, FileDataID 6919242
...
```

### Step 3: FileDataID to Filename

From `manifestinterfacedata_export.txt` (format: `FileDataID: Path,Filename`):

```powershell
# Look up the actual .blp filename for a FileDataID
Select-String -Path "extracted db2\manifestinterfacedata_export.txt" -Pattern "^YOUR_FILEDATAID:"
```

## Tile Naming Convention

WorldMap tiles typically follow this pattern: `[MapName]_[Letter][Number].blp`

- **Letter (A-L)** = Row index (A=0, B=1, ... L=11)
- **Number (1-12)** = Column index

Example: `SomeMap_D3.blp` = Row 3 (D), Column 3

## Building a Complete Map

To reconstruct a full worldmap image:

1. Look up the UIMap ID in `uimapxmapart` to get the UiMapArt ID
2. Query `uimaparttile` for all tiles with that UiMapArt ID
3. For each tile, get the FileDataID and look it up in `manifestinterfacedata`
4. Convert each .blp to .png using `blptopng`
5. Stitch tiles together based on Row/Column positions

### Grid Layout

Tiles are arranged in a grid. For ManaforgeRaid:
- 12 rows (A-L)
- 12 columns (1-12)
- Each tile is typically 256x256 pixels
- Full map = 3072x3072 pixels (12 * 256)

## Aligning Maps to World Coordinates

The `uimapassignment` table contains world coordinate bounds for each map, allowing you to convert between pixel positions and in-game coordinates.

### uimapassignment Format

```
ID: [UVmin],[UVmax],[minX,minY,minZ,maxX,maxY,maxZ],ID,UiMapID,OrderIndex,MapID,...
```

The key fields are the world bounds: `[minX, minY, minZ, maxX, maxY, maxZ]`

The Z range (minZ/maxZ) determines which floor/layer the map applies to (useful for multi-floor dungeons).

### Coordinate Conversion Formulas

**World coordinates to pixel position:**
```
pixelX = (worldX - minX) / (maxX - minX) * imageWidth
pixelY = (worldY - minY) / (maxY - minY) * imageHeight
```

**Pixel position to world coordinates:**
```
worldX = minX + (pixelX / imageWidth) * (maxX - minX)
worldY = minY + (pixelY / imageHeight) * (maxY - minY)
```

### Querying Coordinate Data

```powershell
# Find assignment data for a specific UIMap ID (e.g., 2465)
Select-String -Path "extracted db2\uimapassignment_export.txt" -Pattern ",2465,"
```

## Searching the DB2 Files

To find a specific texture:
```powershell
# Find which file contains a texture name
Select-String -Path "extracted db2\manifestinterfacedata_export.txt" -Pattern "ManaforgeRaid_D3"

# Find tiles for a specific UiMapArt
Select-String -Path "extracted db2\uimaparttile_export.txt" -Pattern ",1975$"

# Find map info
Select-String -Path "extracted db2\uimap_export.txt" -Pattern "^2465:"
```

## Reference Links

- View rendered maps: https://wago.tools/maps/worldmap/[MAPID]
- Example: https://wago.tools/maps/worldmap/2465
