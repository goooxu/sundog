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
BACKEND="$SUNDOG_BUILD/libsundog.so"
GOLDEN_DIR="$ROOT/tests/golden"

WIDTH=256 HEIGHT=256 SPP=64 SEED=7
SCENES=(smoke 01-marble-run 02-cornell-lume 04-parabolica 10-suncatcher 11-glasswork 13-frosted-veil 14-toy-factory)

fail() { echo "make-goldens: FAIL: $*" >&2; exit 1; }
[ -f "$BACKEND" ] || fail "backend not found: $BACKEND"

mkdir -p "$GOLDEN_DIR"

PROBE_TXT="$(python3 "$ROOT/scenes/smoke.py" --probe)"
# Build caliber: the PTX target actually baked into the running library
# (from the build dir's arch stamp) — goldens are only valid for this
# DEVARCH + GPU/driver + AVIF-encode combo, and the manifest records it.
DEVARCH_STAMP="$(basename "$(ls "${SUNDOG_BUILD:-/tmp/sundog-build}"/.arch-* 2>/dev/null | head -1)" 2>/dev/null | sed 's/^\.arch-//')"
echo "$PROBE_TXT"

for s in "${SCENES[@]}"; do
  scene="$ROOT/scenes/$s.py"
  [ -f "$scene" ] || fail "scene not found: $scene"
  echo "== golden: $s (${WIDTH}x${HEIGHT}, $SPP spp, seed $SEED) =="
  python3 "$scene" --out "$GOLDEN_DIR/$s.avif" \
            --size "${WIDTH}x${HEIGHT}" --spp "$SPP" --seed "$SEED" \
            --no-denoise --quiet
  [ -s "$GOLDEN_DIR/$s.avif" ] || fail "empty golden: $GOLDEN_DIR/$s.avif"
done

SCENES_CSV="$(IFS=,; echo "${SCENES[*]}")"
python3 - "$GOLDEN_DIR/manifest.json" "$SCENES_CSV" "$WIDTH" "$HEIGHT" "$SPP" "$SEED" "$PROBE_TXT" "$DEVARCH_STAMP" <<'PY'
import json, sys, datetime
out, scenes, w, h, spp, seed, probe_txt, devarch = sys.argv[1:9]
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
    "build": {"devarch": devarch or "unknown",
              "output": "12-bit PQ BT.2020 lossless AVIF, encoder threads 4"},
}
with open(out, "w") as f:
    json.dump(manifest, f, indent=2)
    f.write("\n")
print("wrote", out)
PY

echo "make-goldens OK (${#SCENES[@]} goldens in $GOLDEN_DIR)"
