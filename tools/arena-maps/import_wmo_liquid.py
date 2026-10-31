"""Build the WMO's liquid surfaces, which the OBJ import leaves out.

The importer only handles terrain liquid (the liquidChunks format written
beside an ADT). A WMO stores its liquid per group inside its own JSON, so the
acid pools that fill these arenas never reach the scene and the floors render
dry.

Each group's liquid is a regular grid: vertX by vertY heights anchored at a
corner, with one flag per tile saying whether that tile is drawn.
"""

import json
import os
import sys

import bpy
from mathutils import Vector

WMO_JSON = sys.argv[sys.argv.index("--") + 1]
WMO_OBJECT = sys.argv[sys.argv.index("--") + 2]

# WoW liquid grids step 1/3 of a 12.5 yard chunk.
TILE_SIZE = 4.1666666
# Bit 3 marks a tile that exists in the grid but is not rendered.
TILE_HIDDEN = 0x08

with open(WMO_JSON, encoding="utf-8") as handle:
    data = json.load(handle)

base = bpy.data.objects.get(WMO_OBJECT)
if base is None:
    raise SystemExit(f"object {WMO_OBJECT} not found")

groups = [g for g in data.get("groups", []) if g.get("liquid")]
print(f"groups carrying liquid: {len(groups)}")

collection = base.users_collection[0] if base.users_collection else bpy.context.scene.collection

# The liquid grid is laid out in the same X/Y the imported geometry uses, only
# its heights are relative to the model origin rather than the scene. Keep the
# meshes unparented and lift them, rather than inheriting the model's rotation
# and sending the grid off onto the wrong axes.
Z_OFFSET = base.matrix_world.translation.z

parent = bpy.data.objects.new("WMO_Liquids", None)
collection.objects.link(parent)
parent.location = (0.0, 0.0, Z_OFFSET)

material = bpy.data.materials.new("WMO_Liquid")
material.use_nodes = True
tree = material.node_tree
tree.nodes.clear()
output = tree.nodes.new("ShaderNodeOutputMaterial")
emission = tree.nodes.new("ShaderNodeEmission")
# Acid green, bright enough to read against the stone from directly above.
emission.inputs["Color"].default_value = (0.35, 0.85, 0.12, 1.0)
emission.inputs["Strength"].default_value = 1.6
tree.links.new(emission.outputs["Emission"], output.inputs["Surface"])

made = 0
tiles_drawn = 0

for index, group in enumerate(groups):
    liquid = group["liquid"]

    vert_x = liquid["vertX"]
    vert_y = liquid["vertY"]
    tile_x = liquid["tileX"]
    tile_y = liquid["tileY"]
    corner = liquid["corner"]
    heights = [v["height"] for v in liquid["vertices"]]
    flags = liquid.get("tiles", [])

    verts = []
    for y in range(vert_y):
        for x in range(vert_x):
            h = heights[y * vert_x + x] if y * vert_x + x < len(heights) else corner[2]
            verts.append((corner[0] + x * TILE_SIZE, corner[1] + y * TILE_SIZE, h))

    faces = []
    for y in range(tile_y):
        for x in range(tile_x):
            i = y * tile_x + x
            if i < len(flags) and (flags[i] & TILE_HIDDEN):
                continue

            a = y * vert_x + x
            b = a + 1
            c = a + vert_x + 1
            d = a + vert_x
            faces.append((a, b, c, d))

    if not faces:
        continue

    mesh = bpy.data.meshes.new(f"liquid_{index}")
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    mesh.materials.append(material)

    obj = bpy.data.objects.new(f"liquid_{index}", mesh)
    collection.objects.link(obj)
    obj.parent = parent

    made += 1
    tiles_drawn += len(faces)

print(f"liquid meshes created: {made}")
print(f"liquid tiles drawn:    {tiles_drawn}")

bpy.ops.wm.save_mainfile()
print("saved")
