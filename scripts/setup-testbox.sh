#!/usr/bin/env bash
# Idempotent test-box bootstrap: user-space CUDA toolkit + OptiX SDK headers.
# Everything lands in /tmp (wiped on reboot — just re-run).
set -euo pipefail

SUNDOG_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DL=/tmp/sundog-dl
mkdir -p "$DL"

# 1. CUDA 13.0 Update 2 toolkit, no sudo (driver 610.47.04 >= bundled 580.95)
RUN=cuda_13.0.2_580.95.05_linux.run
if [ ! -x /tmp/cuda-13.0/bin/nvcc ]; then
  if [ ! -f "$DL/$RUN" ]; then
    echo "== downloading $RUN =="
    wget -q --show-progress -O "$DL/$RUN.part" \
      "https://developer.download.nvidia.com/compute/cuda/13.0.2/local_installers/$RUN"
    mv "$DL/$RUN.part" "$DL/$RUN"
  fi
  echo "== installing CUDA toolkit to /tmp/cuda-13.0 =="
  sh "$DL/$RUN" --silent --toolkit --toolkitpath=/tmp/cuda-13.0 --no-man-page
fi

# 2. OptiX SDK 9.1.0 headers (self-extracting STGZ, no root)
if [ ! -f /tmp/optix-9.1.0/include/optix.h ]; then
  echo "== extracting OptiX SDK to /tmp/optix-9.1.0 =="
  mkdir -p /tmp/optix-9.1.0
  sh "$SUNDOG_ROOT/../NVIDIA-OptiX-SDK-9.1.0-linux64-x86_64.sh" \
    --skip-license --prefix=/tmp/optix-9.1.0 --exclude-subdir
fi

mkdir -p /tmp/sundog-build

echo "== sanity =="
/tmp/cuda-13.0/bin/nvcc --version | tail -1
ls /tmp/optix-9.1.0/include/optix.h
echo "setup-testbox OK"
