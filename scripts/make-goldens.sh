#!/usr/bin/env bash
# sundog golden generator — (re)creates the reference images for run-golden.sh.
#
# Assumes it runs ON THE TEST BOX with $SUNDOG_BUILD/sundog built
# (default /tmp/sundog-build/sundog). Callable from any cwd.
#
# Renders each golden scene at 256x256 / 64 spp / --seed 7 / --no-denoise into
# tests/golden/ and writes tests/golden/manifest.json (scene list, parameters,
# date, --probe summary). Goldens are only valid for the GPU/driver combo in
# the manifest — regenerate after driver or renderer changes.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SUNDOG_BUILD="${SUNDOG_BUILD:-/tmp/sundog-build}"
SUNDOG="$SUNDOG_BUILD/sundog"
GOLDEN_DIR="$ROOT/tests/golden"

WIDTH=256 HEIGHT=256 SPP=64 SEED=7
SCENES=(smoke 01-obsidian-hall 02-cornell-lume 04-parabolica)

fail() { echo "make-goldens: FAIL: $*" >&2; exit 1; }
[ -x "$SUNDOG" ] || fail "binary not found: $SUNDOG"

mkdir -p "$GOLDEN_DIR"

PROBE_TXT="$("$SUNDOG" --probe)"
echo "$PROBE_TXT"

for s in "${SCENES[@]}"; do
  scene="$ROOT/scenes/$s.json"
  [ -f "$scene" ] || fail "scene not found: $scene"
  echo "== golden: $s (${WIDTH}x${HEIGHT}, $SPP spp, seed $SEED) =="
  "$SUNDOG" --scene "$scene" --out "$GOLDEN_DIR/$s.png" \
            --size "${WIDTH}x${HEIGHT}" --spp "$SPP" --seed "$SEED" \
            --no-denoise --quiet
  [ -s "$GOLDEN_DIR/$s.png" ] || fail "empty golden: $GOLDEN_DIR/$s.png"
done

SCENES_CSV="$(IFS=,; echo "${SCENES[*]}")"
python3 - "$GOLDEN_DIR/manifest.json" "$SCENES_CSV" "$WIDTH" "$HEIGHT" "$SPP" "$SEED" "$PROBE_TXT" <<'PY'
import json, sys, datetime
out, scenes, w, h, spp, seed, probe_txt = sys.argv[1:8]
probe = {}
for line in probe_txt.splitlines():
    if ":" in line:
        k, v = line.split(":", 1)
        probe[k.strip()] = v.strip()
manifest = {
    "scenes": scenes.split(","),
    "params": {"width": int(w), "height": int(h), "spp": int(spp),
               "seed": int(seed), "denoise": False},
    "date": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
    "probe": probe,
}
with open(out, "w") as f:
    json.dump(manifest, f, indent=2)
    f.write("\n")
print("wrote", out)
PY

echo "make-goldens OK (${#SCENES[@]} goldens in $GOLDEN_DIR)"
