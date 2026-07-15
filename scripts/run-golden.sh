#!/usr/bin/env bash
# sundog golden test — regression check against tests/golden/ references.
#
# Assumes it runs ON THE TEST BOX with $SUNDOG_BUILD/sundog built
# (default /tmp/sundog-build/sundog). Callable from any cwd.
#
# For each golden scene, renders with the exact golden parameters
# (256x256 / 64 spp / seed 7 / no denoise) into /tmp and compares with
# $SUNDOG_BUILD/img_compare at a 45 dB PSNR threshold. Additionally renders
# smoke.json twice and requires bit-identical PNGs (determinism check).
# Missing goldens => hint to run scripts/make-goldens.sh and exit 1.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SUNDOG_BUILD="${SUNDOG_BUILD:-/tmp/sundog-build}"
SUNDOG="$SUNDOG_BUILD/sundog"
IMG_COMPARE="$SUNDOG_BUILD/img_compare"
GOLDEN_DIR="$ROOT/tests/golden"

WIDTH=256 HEIGHT=256 SPP=64 SEED=7 MIN_PSNR=45
SCENES=(smoke 01-marble-run 02-cornell-lume 04-parabolica 10-suncatcher 11-glasswork)

fail() { echo "run-golden: FAIL: $*" >&2; exit 1; }
[ -x "$SUNDOG" ] || fail "binary not found: $SUNDOG"

# Build img_compare on demand (host-only tool, plain g++).
if [ ! -x "$IMG_COMPARE" ]; then
  echo "== building img_compare =="
  make -C "$ROOT" SUNDOG_BUILD="$SUNDOG_BUILD" "$IMG_COMPARE" \
    || fail "could not build $IMG_COMPARE"
fi

TMP="$(mktemp -d /tmp/sundog-golden.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

render() { # render SCENE OUT
  "$SUNDOG" --scene "$ROOT/scenes/$1.json" --out "$2" \
            --size "${WIDTH}x${HEIGHT}" --spp "$SPP" --seed "$SEED" \
            --no-denoise --quiet
}

echo "== golden comparisons (min PSNR $MIN_PSNR dB) =="
for s in "${SCENES[@]}"; do
  golden="$GOLDEN_DIR/$s.png"
  if [ ! -f "$golden" ]; then
    echo "run-golden: missing golden $golden" >&2
    echo "run-golden: generate references first: scripts/make-goldens.sh" >&2
    exit 1
  fi
  [ -f "$ROOT/scenes/$s.json" ] || fail "scene not found: $ROOT/scenes/$s.json"
  render "$s" "$TMP/$s.png"
  printf '%-20s ' "$s"
  "$IMG_COMPARE" "$golden" "$TMP/$s.png" "$MIN_PSNR" \
    || fail "$s below $MIN_PSNR dB vs golden (renderer output changed?)"
done

echo "== determinism: smoke rendered twice must be bit-identical =="
render smoke "$TMP/det-a.png"
render smoke "$TMP/det-b.png"
sha_a=$(sha256sum "$TMP/det-a.png" | cut -d' ' -f1)
sha_b=$(sha256sum "$TMP/det-b.png" | cut -d' ' -f1)
echo "  run A: $sha_a"
echo "  run B: $sha_b"
[ "$sha_a" = "$sha_b" ] || fail "non-deterministic output for fixed seed"

echo "run-golden OK (${#SCENES[@]} scenes + determinism)"
