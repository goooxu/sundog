#!/usr/bin/env bash
# sundog compute-sanitizer check — memcheck + initcheck over a small render.
#
# Assumes it runs ON THE TEST BOX with $SUNDOG_BUILD/sundog built and a CUDA
# toolkit at $CUDA_HOME (default /tmp/cuda-13.0, which provides
# $CUDA_HOME/bin/compute-sanitizer). Callable from any cwd.
#
# Runs smoke.py at 64x64 / 4 spp under:
#   1. --tool memcheck   (invalid accesses, leaks)
#   2. --tool initcheck  (reads of uninitialized device memory)
# Both with --error-exitcode 1, so any sanitizer finding fails the script.
#
# Known initcheck false positive (filtered below): the compacted-size readback
# in accel.cpp buildAndCompact(). optixAccelBuild() writes that device buffer
# via the emitted OPTIX_PROPERTY_TYPE_COMPACTED_SIZE property, but OptiX's
# internal writes are invisible to initcheck, so the following cudaMemcpy is
# flagged as reading "uninitialized" memory. The value is demonstrably valid
# (GAS compaction succeeds with it). Any OTHER initcheck error still fails.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SUNDOG_BUILD="${SUNDOG_BUILD:-/tmp/sundog-build}"
BACKEND="$SUNDOG_BUILD/libsundog.so"
CUDA_HOME="${CUDA_HOME:-/tmp/cuda-13.0}"
SANITIZER="$CUDA_HOME/bin/compute-sanitizer"
SCENE_PY="$ROOT/scenes/smoke.py"

fail() { echo "run-sanitizer: FAIL: $*" >&2; exit 1; }
[ -f "$BACKEND" ]   || fail "backend not found: $BACKEND"
[ -x "$SANITIZER" ] || fail "compute-sanitizer not found: $SANITIZER"
[ -f "$SCENE_PY" ]  || fail "scene not found: $SCENE_PY"

TMP="$(mktemp -d /tmp/sundog-sanitizer.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

# compute-sanitizer wraps the python process: rendering happens in-process
# through libsundog.so (ctypes), so the CUDA work is fully instrumented; the
# interpreter itself does no GPU work and adds no sanitizer noise.
echo "== compute-sanitizer --tool memcheck (smoke 64x64 / 4 spp) =="
"$SANITIZER" --tool memcheck --error-exitcode 1 \
  python3 "$SCENE_PY" --out "$TMP/smoke-memcheck.avif" \
          --size 64x64 --spp 4 --quiet \
  || fail "memcheck reported errors"

echo "== compute-sanitizer --tool initcheck (smoke 64x64 / 4 spp) =="
rc=0
"$SANITIZER" --tool initcheck --error-exitcode 1 \
  python3 "$SCENE_PY" --out "$TMP/smoke-initcheck.avif" \
          --size 64x64 --spp 4 --quiet \
  > "$TMP/initcheck.log" 2>&1 || rc=$?
if [ "$rc" -ne 0 ]; then
  # Tolerate only the known optixAccelBuild compacted-size false positive.
  python3 - "$TMP/initcheck.log" <<'PY' || { cat "$TMP/initcheck.log"; exit 1; }
import re, sys
log = open(sys.argv[1]).read()
# split sanitizer output into error records (each starts with an error header)
blocks = re.split(r"(?=^========= (?:Host API memory access error|Uninitialized))",
                  log, flags=re.M)
errors = [b for b in blocks if b.startswith("========= Host API") or
                               b.startswith("========= Uninitialized")]
fp = [b for b in errors
      if "on access by cudaMemcpy source" in b and "buildAndCompact" in b]
m = re.search(r"ERROR SUMMARY: (\d+) error", log)
total = int(m.group(1)) if m else -1
if total == len(fp) == len(errors) and total > 0:
    print(f"  initcheck: {total} known false positive(s) filtered "
          "(optixAccelBuild compacted-size readback), no real errors")
    sys.exit(0)
print(f"  initcheck: {total} error(s), only {len(fp)} match the known "
      "false-positive signature", file=sys.stderr)
sys.exit(1)
PY
fi

echo "run-sanitizer OK (memcheck + initcheck clean)"
