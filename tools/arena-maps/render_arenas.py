"""Render the top-down arena map for every boss in a dungeon scene.

Run inside Blender on a scene prepared as README.md describes:

    blender -b <scene>.blend --python render_arenas.py -- <out-dir>
        [--framing framing/<dungeon>.json] [--encounters <encounters.json>]

--framing holds the camera position, scale and floor picked for each boss with
sweep_arena.py. --encounters is the boss position file headless-encounters.js
writes; any boss it lists that the framing file does not gets a first-guess
render at its journal position, scale 250 and the highest floor found there.
At least one of the two is required.
"""

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy

import arena_scene

DEFAULT_SCALE = 250.0

parser = argparse.ArgumentParser(prog="render_arenas.py")
parser.add_argument("out_dir")
parser.add_argument("--framing")
parser.add_argument("--encounters")
args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
if not args.framing and not args.encounters:
    parser.error("give --framing, --encounters or both")

arenas = {}
if args.encounters:
    with open(args.encounters, encoding="utf-8") as handle:
        for encounter in json.load(handle)["encounters"]:
            if encounter.get("encounter_id") is None:
                print(f"skipped {encounter['label']}: no encounter id")
                continue
            # Exported vertices are world yards with the axes swapped for Z-up.
            arenas[encounter["encounter_id"]] = {
                "label": encounter["label"],
                "x": encounter["world_y"],
                "y": -encounter["world_x"],
                "scale": DEFAULT_SCALE,
            }

if args.framing:
    with open(args.framing, encoding="utf-8") as handle:
        for key, framed in json.load(handle)["arenas"].items():
            arenas[int(key)] = framed

os.makedirs(args.out_dir, exist_ok=True)
scene, camera = arena_scene.prepare(resolution=1024)

for encounter_id, arena in sorted(arenas.items()):
    x, y, scale = arena["x"], arena["y"], arena["scale"]
    label = arena.get("label", "")

    if "camera_z" in arena:
        # Picked directly in a sweep, for rooms where no floor/ceiling pair
        # gives a clear view.
        floor, height = None, arena["camera_z"]
    else:
        floor, height = arena_scene.camera_height(scene, x, y, arena.get("floor"))
        if height is None:
            print(f"skipped {label}: no geometry at {x:.0f}, {y:.0f}")
            continue

    arena_scene.show_wmo_liquid(arena.get("liquid", True))
    camera.location = (x, y, height)
    camera.data.ortho_scale = scale

    name = arena_scene.map_file_name(encounter_id, x, y, scale)
    scene.render.filepath = os.path.join(args.out_dir, name)
    bpy.ops.render.render(write_still=True)

    floor_text = f"{floor:7.1f}" if floor is not None else "  fixed"
    print(f"{label[:26]:26} floor={floor_text} cam_z={height:7.1f}  {name}")

print(f"done: {len(arenas)} arenas")
