#!/usr/bin/env python3
"""
Bake the extracted season OBJ models into a single binary pack the app loads
with zero parsing: data/season_models_<ver>.awmp

Why: the raw exports are ~400MB of OBJ text. Parsing that at runtime (TinyObj +
normal generation + normalization) is exactly the load cost the baked pack
removes. The bake pre-applies everything CustomModelLoader would do:

  - decimates to an actor-appropriate triangle budget (these render as small
    actor pucks, not hero models - full-res M2s are pure waste)
  - normalizes to a unit cube centered at the origin (CustomModelLoader::
    normalizeModel semantics: bbox center, scale 1/maxDim)
  - computes area-weighted smooth normals
  - stores vertices in the app's exact Vertex3D wire layout
    (pos[3]f, normal[3]f, color[3]f, uv[2]f = 44 bytes) + uint32 indices

so the runtime path is: pack bytes -> GPU staging. Nothing else.

The creature-name -> model assignment table (from season_nameGroup_assignments
.json) is embedded in the same pack: one file, one loader, no JSON at runtime.

Assignments are keyed by npcId, not creature name: names carry a per-spawn
instance suffix at runtime ("Venom Leech (12)") and are localized, so neither
matches a baked table. The npcId comes straight out of the unit GUID.

Format (little-endian, offsets absolute, blobs 4-byte aligned):
  header:  'AWMP' u32 version=2  u32 modelCount  u32 assignCount
  models:  [u16 nameLen, name, u32 vertCount, u32 idxCount,
            u64 vertOffset, u64 idxOffset] * modelCount
  assigns: [u32 npcId, u16 mLen, modelName] * assignCount
  blobs

Usage:
  python bake_season_models.py [--models season_models] [--assign season_nameGroup_assignments.json]
                               [--out ../../AwowLab/data/season_models_12.1.awmp]
                               [--budget 6000]
"""

import argparse
import json
import math
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def parse_obj(path):
    """Positions + triangle indices. Normals/uvs ignored (recomputed/unused)."""
    verts = []
    faces = []
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith("v "):
                p = line.split()
                verts.append((float(p[1]), float(p[2]), float(p[3])))
            elif line.startswith("f "):
                # tokens are v, v/vt, v//vn or v/vt/vn; 1-based. Fan-triangulate.
                idx = []
                for tok in line.split()[1:]:
                    s = tok.split("/", 1)[0]
                    i = int(s)
                    idx.append(i - 1 if i > 0 else len(verts) + i)
                for k in range(1, len(idx) - 1):
                    faces.append((idx[0], idx[k], idx[k + 1]))
    return verts, faces


# Built-in actor shapes are ~2.0 world units across (sphere radius 1.0, cube
# edge baseSize*2). Models must occupy a comparable silhouette or the
# health-based scale multiplier stops reading visually.
#
# A mesh only fills part of its bounding box - a humanoid's arms and legs leave
# big gaps, measuring ~60% of the box a sphere fills solid - so matching the box
# to 2.0 still reads visibly smaller than a sphere. Overshoot the footprint to
# compensate for that sparsity.
TARGET_FOOTPRINT = 2.6

# Height is capped so tall models don't become spires, but the cap must not
# starve the footprint: enforcing it by uniform scale shrank long/tall meshes
# (snakes, spires) to a 0.26-wide footprint. Instead the cap is applied as a
# separate non-uniform squash on the vertical axis only, leaving the footprint
# at target.
MAX_HEIGHT = 3.0


def _bbox(verts):
    xs = [v[0] for v in verts]
    ys = [v[1] for v in verts]
    zs = [v[2] for v in verts]
    return xs, ys, zs


def normalize(verts):
    """Unit-cube fit (max dimension 1.0) - the clusterer expects [-0.5, 0.5]."""
    xs, ys, zs = _bbox(verts)
    cx = (min(xs) + max(xs)) * 0.5
    cy = (min(ys) + max(ys)) * 0.5
    cz = (min(zs) + max(zs)) * 0.5
    max_dim = max(max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs), 1e-3)
    s = 1.0 / max_dim
    return [((x - cx) * s, (y - cy) * s, (z - cz) * s) for x, y, z in verts]


def normalize_footprint(verts):
    """Final sizing: footprint -> TARGET_FOOTPRINT, height squashed if over cap.

    Centered horizontally but rested on y=0 (feet on the ground) rather than
    centered vertically. The renderer places actors at ground level and scales
    about the origin, so a vertically-centered mesh sinks half its height into
    the floor - and worse, the amount it sinks changes with the health-based
    scale. Anchoring the base at the origin makes it sit correctly at any scale
    with no per-model offset to maintain.
    """
    xs, ys, zs = _bbox(verts)
    cx = (min(xs) + max(xs)) * 0.5
    cz = (min(zs) + max(zs)) * 0.5
    base_y = min(ys)
    footprint = max(max(xs) - min(xs), max(zs) - min(zs), 1e-3)
    height = max(max(ys) - min(ys), 1e-3)

    # Uniform scale sets the footprint...
    s = TARGET_FOOTPRINT / footprint
    # ...then squash the vertical axis alone if the result would be too tall,
    # so a tall mesh loses height instead of footprint.
    sy = s
    if height * s > MAX_HEIGHT:
        sy = MAX_HEIGHT / height

    return [((x - cx) * s, (y - base_y) * sy, (z - cz) * s) for x, y, z in verts]


def cluster(verts, faces, grid):
    """Vertex-clustering decimation on a grid^3 lattice over the unit cube.

    Returns (newVerts, newFaces). Representative = mean of member vertices.
    """
    cell_of = []
    for x, y, z in verts:
        cx = min(int((x + 0.5) * grid), grid - 1)
        cy = min(int((y + 0.5) * grid), grid - 1)
        cz = min(int((z + 0.5) * grid), grid - 1)
        cell_of.append((cx << 20) | (cy << 10) | cz)

    # accumulate representative position per cell
    acc = {}
    for i, (x, y, z) in enumerate(verts):
        c = cell_of[i]
        a = acc.get(c)
        if a is None:
            acc[c] = [x, y, z, 1]
        else:
            a[0] += x; a[1] += y; a[2] += z; a[3] += 1

    cell_index = {}
    new_verts = []
    for c, (sx, sy, sz, n) in acc.items():
        cell_index[c] = len(new_verts)
        new_verts.append((sx / n, sy / n, sz / n))

    new_faces = []
    seen = set()
    for a, b, c in faces:
        ca, cb, cc = cell_of[a], cell_of[b], cell_of[c]
        if ca == cb or cb == cc or ca == cc:
            continue  # collapsed
        tri = (cell_index[ca], cell_index[cb], cell_index[cc])
        key = tuple(sorted(tri))
        if key in seen:
            continue
        seen.add(key)
        new_faces.append(tri)
    return new_verts, new_faces


def decimate(verts, faces, budget):
    """Cluster at decreasing resolution until under the triangle budget."""
    if len(faces) <= budget:
        return verts, faces
    grid = 128
    best = None
    while grid >= 8:
        nv, nf = cluster(verts, faces, grid)
        if len(nf) <= budget:
            # guard against nuking small models into nothing
            if len(nf) >= 32 or best is None:
                return nv, nf
            return best
        best = (nv, nf)
        grid = int(grid * 0.8)
    return best if best else (verts, faces)


def smooth_normals(verts, faces):
    """Area-weighted vertex normals (cross-product magnitude weights)."""
    n = [[0.0, 0.0, 0.0] for _ in verts]
    for a, b, c in faces:
        ax, ay, az = verts[a]
        bx, by, bz = verts[b]
        cx, cy, cz = verts[c]
        ux, uy, uz = bx - ax, by - ay, bz - az
        vx, vy, vz = cx - ax, cy - ay, cz - az
        fx = uy * vz - uz * vy
        fy = uz * vx - ux * vz
        fz = ux * vy - uy * vx
        for i in (a, b, c):
            n[i][0] += fx; n[i][1] += fy; n[i][2] += fz
    out = []
    for x, y, z in n:
        l = math.sqrt(x * x + y * y + z * z)
        out.append((x / l, y / l, z / l) if l > 1e-12 else (0.0, 0.0, 1.0))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--models", default=str(HERE / "season_models"))
    ap.add_argument("--assign", default=str(HERE / "season_nameGroup_assignments.json"))
    ap.add_argument("--out", default=str(HERE.parent.parent / "AwowLab" / "data" / "season_models_12.1.awmp"))
    ap.add_argument("--budget", type=int, default=6000)
    args = ap.parse_args()

    model_dir = Path(args.models)
    objs = sorted(model_dir.glob("model_*.obj"))
    if not objs:
        sys.exit(f"no model_*.obj in {model_dir}")

    baked = []  # (name, vertsBytes, idxBytes, vertCount, idxCount)
    in_tris = out_tris = 0
    for i, obj in enumerate(objs, 1):
        name = obj.stem
        verts, faces = parse_obj(obj)
        if not verts or not faces:
            print(f"  ! {name}: empty, skipped")
            continue
        in_tris += len(faces)
        verts = normalize(verts)
        verts, faces = decimate(verts, faces, args.budget)
        verts = normalize_footprint(verts)  # final sizing to match built-in shapes
        normals = smooth_normals(verts, faces)
        out_tris += len(faces)

        vb = bytearray()
        for (px, py, pz), (nx, ny, nz) in zip(verts, normals):
            vb += struct.pack("<11f", px, py, pz, nx, ny, nz, 1.0, 1.0, 1.0, 0.0, 0.0)
        ib = bytearray()
        for a, b, c in faces:
            ib += struct.pack("<3I", a, b, c)
        baked.append((name, bytes(vb), bytes(ib), len(verts), len(faces) * 3))
        if i % 25 == 0:
            print(f"  {i}/{len(objs)} baked...")

    # Assignments keyed by npcId (name is suffixed/localized at runtime).
    # Only keep entries whose model actually baked and that have a real npcId;
    # boss rows resolved via the journal chain have no npcId and are skipped.
    have = {b[0] for b in baked}
    assigns = []
    skipped_no_npc = 0
    ap_path = Path(args.assign)
    if ap_path.exists():
        data = json.loads(ap_path.read_text(encoding="utf-8"))
        for creature, info in data.items():
            model = info.get("customShapeName", "")
            npc = info.get("npcId")
            if model not in have:
                continue
            if not npc:
                skipped_no_npc += 1
                continue
            assigns.append((int(npc), model))
    assigns.sort()

    # ---- write pack ----
    names = [b[0].encode("utf-8") for b in baked]
    table_size = sum(2 + len(n) + 4 + 4 + 8 + 8 for n in names)
    assign_blob = bytearray()
    for npc_id, model in assigns:
        mb = model.encode("utf-8")
        assign_blob += struct.pack("<I", npc_id) + struct.pack("<H", len(mb)) + mb

    off = 16 + table_size + len(assign_blob)
    off += (-off) % 4  # align first blob

    table = bytearray()
    blobs = bytearray()
    blob_base = off
    for (name, vb, ib, vcount, icount), nb in zip(baked, names):
        voff = blob_base + len(blobs)
        blobs += vb
        blobs += b"\0" * ((-len(blobs)) % 4)
        ioff = blob_base + len(blobs)
        blobs += ib
        blobs += b"\0" * ((-len(blobs)) % 4)
        table += struct.pack("<H", len(nb)) + nb
        table += struct.pack("<IIQQ", vcount, icount, voff, ioff)

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(b"AWMP")
        f.write(struct.pack("<III", 2, len(baked), len(assigns)))
        f.write(table)
        f.write(assign_blob)
        f.write(b"\0" * ((-(16 + len(table) + len(assign_blob))) % 4))
        f.write(blobs)

    size_mb = out_path.stat().st_size / 1048576
    print(f"\nbaked {len(baked)} models: {in_tris:,} -> {out_tris:,} tris "
          f"({100 * out_tris // max(in_tris, 1)}%)")
    print(f"assignments embedded: {len(assigns)} (by npcId; "
          f"{skipped_no_npc} skipped with no npcId)")
    print(f"wrote {out_path} ({size_mb:.1f} MB)")


if __name__ == "__main__":
    main()
