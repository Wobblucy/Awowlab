"""Lay sweep shots out on one image so the best framing can be picked.

One row per encounter, one cell per shot, each labelled with the scale and
height in its file name. Runs with plain Python (needs Pillow), not Blender.

    python contact_sheet.py <sweep-dir> [<sweep-dir> ...] -o sheet.png
"""

import argparse
import collections
import glob
import os

from PIL import Image, ImageDraw

CELL = 300
PAD = 26

parser = argparse.ArgumentParser()
parser.add_argument("dirs", nargs="+")
parser.add_argument("-o", "--output", default="contact_sheet.png")
args = parser.parse_args()

rows = collections.OrderedDict()
for directory in args.dirs:
    for path in sorted(glob.glob(os.path.join(directory, "*.png"))):
        encounter_id = os.path.basename(path).split("_")[0]
        rows.setdefault(encounter_id, []).append(path)

if not rows:
    raise SystemExit("no shots found")

columns = max(len(paths) for paths in rows.values())
sheet = Image.new("RGB", (columns * CELL + PAD, len(rows) * (CELL + PAD) + PAD),
                  (24, 24, 26))
draw = ImageDraw.Draw(sheet)

for row, (encounter_id, paths) in enumerate(rows.items()):
    top = PAD + row * (CELL + PAD)
    draw.text((4, top - 16), encounter_id, fill=(230, 230, 120))
    for column, path in enumerate(paths):
        shot = Image.open(path).convert("RGB").resize((CELL - 8, CELL - 8))
        sheet.paste(shot, (column * CELL + 4, top))
        label = os.path.basename(path)[len(encounter_id) + 1:-len(".png")]
        draw.text((column * CELL + 8, top + CELL - 16), label, fill=(255, 255, 255))

sheet.save(args.output)
print(f"{len(rows)} rows, up to {columns} shots each -> {args.output}")
