#!/usr/bin/env bash
# sundog smoke test — fast end-to-end sanity check.
#
# Assumes it runs ON THE TEST BOX (RTX GPU + NVIDIA driver), with the binary
# already built at $SUNDOG_BUILD/sundog (default /tmp/sundog-build/sundog).
# Callable from any cwd; the repo root is derived from this script's path.
#
# Checks:
#   1. --probe runs and reports a GPU
#   2. smoke.py renders at 64x64 / 4 spp and produces an AVIF > 1 KB
#   3. the --denoise variant also renders
#   4. --stats writes parseable JSON
# Any failure exits non-zero.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SUNDOG_BUILD="${SUNDOG_BUILD:-/tmp/sundog-build}"
BACKEND="$SUNDOG_BUILD/libsundog.so"
SCENE="$ROOT/scenes/smoke.py"

fail() { echo "run-smoke: FAIL: $*" >&2; exit 1; }

[ -f "$BACKEND" ] || fail "backend not found: $BACKEND (build it on the test box first)"
[ -f "$SCENE" ]  || fail "scene not found: $SCENE"

TMP="$(mktemp -d /tmp/sundog-smoke.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

check_avif() { # check_avif FILE
  [ -f "$1" ] || fail "missing output: $1"
  local sz
  sz=$(stat -c %s "$1")
  [ "$sz" -gt 1024 ] || fail "$1 too small ($sz bytes <= 1024)"
  echo "  ok: $1 ($sz bytes)"
}

echo "== 1. probe =="
python3 "$SCENE" --probe | tee "$TMP/probe.txt"
grep -q '^GPU:' "$TMP/probe.txt" || fail "--probe did not report a GPU"

echo "== 2. render smoke.py 64x64 / 4 spp =="
python3 "$SCENE" --out "$TMP/smoke.avif" --size 64x64 --spp 4 --quiet
check_avif "$TMP/smoke.avif"

echo "== 3. denoise variant =="
python3 "$SCENE" --out "$TMP/smoke-dn.avif" --size 64x64 --spp 4 --denoise --quiet
check_avif "$TMP/smoke-dn.avif"

echo "== 4. stats JSON =="
python3 "$SCENE" --out "$TMP/smoke-st.avif" --size 64x64 --spp 4 --quiet \
          --stats "$TMP/smoke.stats.json"
[ -f "$TMP/smoke.stats.json" ] || fail "missing stats json"
python3 -c '
import json, sys
d = json.load(open(sys.argv[1]))
for key in ("timings_ms", "rays_traced", "device"):
    assert key in d, f"stats json missing key {key!r}"
print("  ok: stats json parses, render =", d["timings_ms"]["render"], "ms")
' "$TMP/smoke.stats.json" || fail "stats json invalid"

echo "run-smoke OK"
