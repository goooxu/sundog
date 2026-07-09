#!/usr/bin/env bash
# cxxrt CPU baseline build — the original CPU ray tracer sundog was rewritten
# from, used as the reference for scripts/run-benchmark.sh.
#
# Assumes it runs ON THE TEST BOX (needs network access for the lodepng clone
# and a g++ with OpenMP). Idempotent: safe to re-run, only does missing steps.
# Callable from any cwd.
#
# Steps:
#   1. unpack $CXXRT_TARBALL into /tmp/cxxrt-baseline (strips the top dir)
#   2. git clone lodepng into 3rd-party/lodepng (the tarball ships it as an
#      empty submodule dir; the Makefile compiles 3rd-party/lodepng/lodepng.cpp)
#   3. make -j16 with CFLAGS overridden to -O3 -march=native (the upstream
#      Makefile uses `CFLAGS = -std=c++17 ... -O2 -fPIC -fopenmp`, so the
#      override must repeat the non-optimization flags)
#   4. list bin/example1..5
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CXXRT_TARBALL="${CXXRT_TARBALL:-$(dirname "$ROOT")/raytracing-master.tar.gz}"
DEST="${CXXRT_BASELINE:-/tmp/cxxrt-baseline}"
LODEPNG_URL="https://github.com/lvandeve/lodepng"

fail() { echo "build-cpu-baseline: FAIL: $*" >&2; exit 1; }

# 1. unpack
if [ ! -f "$DEST/Makefile" ]; then
  [ -f "$CXXRT_TARBALL" ] || fail "tarball not found: $CXXRT_TARBALL"
  echo "== unpacking $CXXRT_TARBALL -> $DEST =="
  mkdir -p "$DEST"
  tar -xzf "$CXXRT_TARBALL" -C "$DEST" --strip-components=1
else
  echo "== $DEST already unpacked =="
fi

# 2. lodepng (submodule in the original repo, empty in the tarball)
if [ ! -f "$DEST/3rd-party/lodepng/lodepng.cpp" ]; then
  echo "== cloning lodepng =="
  rm -rf "$DEST/3rd-party/lodepng"
  git clone --depth 1 "$LODEPNG_URL" "$DEST/3rd-party/lodepng"
else
  echo "== lodepng already present =="
fi

# 3. build (override CFLAGS: keep upstream flags, bump -O2 -> -O3 -march=native)
echo "== building (make -j16, -O3 -march=native) =="
make -C "$DEST" -j16 \
  CFLAGS='-std=c++17 -Wall -Wfatal-errors -O3 -march=native -fPIC -fopenmp'

# 4. verify
echo "== examples =="
for i in 1 2 3 4 5; do
  [ -x "$DEST/bin/example$i" ] || fail "missing $DEST/bin/example$i"
done
ls -l "$DEST"/bin/example{1..5}
echo "build-cpu-baseline OK ($DEST)"
