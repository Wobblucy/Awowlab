"""Render one arena several ways so the best framing can be picked by eye.

The journal gives a boss position but no height, and these dungeons stack
rooms, so which floor to frame cannot be inferred reliably. This renders one
shot per combination of scale and height; contact_sheet.py lays them out side
by side, and the chosen values go into the dungeon's framing file.

    blender -b <scene>.blend --python sweep_arena.py -- <out-dir>
        --id <encounter id> --at <x> <y> --scales 200 240 300
        [--floors 438 466 518 | --camera-z 560 575 590]

--floors frames each candidate floor with the camera placed under the storey
above it. --camera-z sets the camera height directly, for open rooms where no
floor/ceiling pair gives a clean view. With neither, the highest floor found at
the point is used.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bpy

import arena_scene

parser = argparse.ArgumentParser(prog="sweep_arena.py")
parser.add_argument("out_dir")
parser.add_argument("--id", type=int, required=True)
parser.add_argument("--at", type=float, nargs=2, required=True, metavar=("X", "Y"))
parser.add_argument("--scales", type=float, nargs="+", default=[250.0])
heights = parser.add_mutually_exclusive_group()
heights.add_argument("--floors", type=float, nargs="+")
heights.add_argument("--camera-z", type=float, nargs="+")
parser.add_argument("--resolution", type=int, default=768,
                    help="smaller than the final 1024 to keep sweeps quick")
args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])

os.makedirs(args.out_dir, exist_ok=True)
scene, camera = arena_scene.prepare(resolution=args.resolution)
x, y = args.at

shots = []
if args.camera_z:
    shots = [(None, z) for z in args.camera_z]
else:
    for expected in (args.floors or [None]):
        floor, height = arena_scene.camera_height(scene, x, y, expected)
        if height is None:
            sys.exit(f"no geometry at {x:.0f}, {y:.0f}")
        shots.append((floor, height))

for scale in args.scales:
    camera.data.ortho_scale = scale
    for floor, height in shots:
        camera.location = (x, y, height)
        floor_part = f"_floor{int(round(floor))}" if floor is not None else ""
        name = f"{args.id}_s{int(round(scale))}{floor_part}_cam{int(round(height))}.png"
        scene.render.filepath = os.path.join(args.out_dir, name)
        bpy.ops.render.render(write_still=True)
        print(f"{args.id}  scale={scale:5.0f}  cam_z={height:7.1f}  {name}")

print(f"sweep done: {len(args.scales) * len(shots)} shots")
