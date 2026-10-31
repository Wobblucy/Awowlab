#!/usr/bin/env bash
# Extract this season's world map tiles from the WoW install.
#
# Opening the CASC storage costs ~4 seconds, so extract in ONE invocation with a
# listfile and a glob rather than looping over --fdid: 252 separate calls take
# ~18 minutes, the batch takes ~13 seconds.
#
# The listfile maps fileDataID -> virtual path. Write it with
#   list_required_files.py <uimap ids> --listfile season_<version>_listfile.csv
#
# Product matters: `wow` is retail, `wowt` is the PTR. They share one CASC store
# under a single install root, and retail can be a full patch behind - extracting
# with the wrong product silently yields the previous season's art. Check
# .build.info at the install root for which product carries the version you want.
#
# Usage: ./extract_season_tiles.sh [product]
#
# Settings, all optional:
#   AWOWLAB_WOW_DIR   WoW install root (default: C:/Program Files (x86)/World of Warcraft)
#   AWOWLAB_LISTFILE  fileDataID listfile to extract (default: season_12.1_listfile.csv here)
#   RUSTYDEMON_CLI    rustydemon-cli executable (default: on PATH, else a
#                     rustydemon checkout built beside this repository)

set -u

PRODUCT="${1:-wowt}"
HERE="$(cd "$(dirname "$0")" && pwd)"
# Stock rustydemon-cli from https://github.com/HoldMyBeer-gg/rustydemon .
CLI="${RUSTYDEMON_CLI:-$(command -v rustydemon-cli || echo "$HERE/../../../rustydemon/target/release/rustydemon-cli.exe")}"
WOW="${AWOWLAB_WOW_DIR:-C:/Program Files (x86)/World of Warcraft}"
OUT="$HERE/tiles_blp"
LISTFILE="${AWOWLAB_LISTFILE:-$HERE/season_12.1_listfile.csv}"

[ -x "$CLI" ] || {
    echo "rustydemon-cli not found at $CLI"
    echo "Build it from https://github.com/HoldMyBeer-gg/rustydemon with"
    echo "  cargo build --release -p rustydemon-cli"
    echo "then put it on PATH or set RUSTYDEMON_CLI to the executable."
    exit 1
}
[ -d "$WOW" ] || { echo "WoW install not found at $WOW - set AWOWLAB_WOW_DIR to the install root"; exit 1; }
[ -f "$LISTFILE" ] || { echo "missing $LISTFILE - set AWOWLAB_LISTFILE, or generate it (see the header above)"; exit 1; }

mkdir -p "$OUT"
echo "Extracting world map tiles (product: $PRODUCT)"

"$CLI" export \
    -a "$WOW" \
    --product "$PRODUCT" \
    --listfile "$LISTFILE" \
    --path "Interface/WorldMap/**" \
    -o "$OUT" \
    --flat --overwrite 2>&1 | grep -vE "^open .*: config error" | tail -3

want=$(wc -l < "$LISTFILE")
got=$(ls "$OUT" 2>/dev/null | wc -l)
echo
echo "$got / $want tiles present in $OUT"

if [ "$got" -lt "$want" ]; then
    echo
    echo "Tiles are missing. A 'read <hash> failed locally ... and via CDN (404)'"
    echo "error means those chunks were never downloaded - the install is partial."
    echo "Run a Battle.net Scan and Repair on this product, then re-run this script."
fi
