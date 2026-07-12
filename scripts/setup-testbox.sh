#!/usr/bin/env bash
# Idempotent test-box bootstrap: user-space CUDA toolkit + OptiX SDK headers
# + PhysX 5.8 (GPU). Everything lands in /tmp (wiped on reboot — just re-run).
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

# 3. PhysX 5.8.0 (GPU): static SDK libs + libPhysXGpu_64.so, all built from
# source (the GPU code is open since 5.6; CUDA kernels include sm_120).
# Fast path: restore the assembled prefix from the NFS tarball. Slow path
# (first time only): build from the NFS source tarball — generate_projects
# needs outbound HTTPS to packman's CDN for its own cmake/python — then
# cache the prefix back to NFS so no network is ever needed again.
PHYSX_TARBALL="$SUNDOG_ROOT/../physx-5.8.0-linux-gpu.tar.gz"
PHYSX_SRC_TARBALL="$SUNDOG_ROOT/../physx-src-5.8.0.tar.gz"
if [ ! -f /tmp/physx-5.8/lib/libPhysX_static_64.a ] || [ ! -f /tmp/physx-5.8/bin/libPhysXGpu_64.so ]; then
  if [ -f "$PHYSX_TARBALL" ]; then
    echo "== restoring PhysX prefix from NFS tarball =="
    tar -C /tmp -xzf "$PHYSX_TARBALL"
  else
    [ -f "$PHYSX_SRC_TARBALL" ] || { echo "missing $PHYSX_SRC_TARBALL" >&2; exit 1; }
    echo "== building PhysX 5.8.0 from source (one-time) =="
    rm -rf /tmp/physx-src /tmp/physx-5.8-plan
    tar -C /tmp -xzf "$PHYSX_SRC_TARBALL"
    mv /tmp/physx-5.8-plan /tmp/physx-src
    PRESET=/tmp/physx-src/physx/buildtools/presets/public/linux-gcc.xml
    # Reduced arch list (sm_80+): the full list starts at sm_70, which nvcc 13
    # no longer accepts. Skip snippets/PVD runtime — we only need the SDK.
    sed -i 's/\("PX_GENERATE_GPU_REDUCED_ARCHITECTURES" value="\)False/\1True/;
            s/\("PX_BUILDSNIPPETS" value="\)True/\1False/;
            s/\("PX_BUILDPVDRUNTIME" value="\)True/\1False/' "$PRESET"
    # PhysX 5.8 predates CUDA 13; don't let new nvcc warnings fail the build.
    sed -i 's/ -Werror=all-warnings//' \
      /tmp/physx-src/physx/source/compiler/cmakegpu/CMakeLists.txt
    # CUDA 13 promoted cuCtxCreate to the 4-arg _v4 form (extra
    # CUctxCreateParams*); PhysX 5.8 still calls the 3-arg CUDA 12 form.
    sed -i 's/cuCtxCreate(&mCtx, (unsigned int)flags, mDevHandle)/cuCtxCreate(\&mCtx, (CUctxCreateParams*)0, (unsigned int)flags, mDevHandle)/' \
      /tmp/physx-src/physx/source/cudamanager/src/CudaContextManager.cpp
    # CUDA 13's cudaGL.h unconditionally includes <GL/gl.h>; the box has no
    # GL dev headers (no sudo). Drop a minimal typedef stub into our own
    # user-space CUDA install — PhysX itself only needs the GL scalar types.
    GLSTUB=/tmp/cuda-13.0/targets/x86_64-linux/include/GL/gl.h
    if [ ! -f "$GLSTUB" ]; then
      mkdir -p "$(dirname "$GLSTUB")"
      cat > "$GLSTUB" <<'EOF'
/* Minimal GL type stub for cudaGL.h on boxes without GL dev headers. */
#ifndef __gl_h_
#define __gl_h_
typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef void GLvoid;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;
#endif
EOF
    fi
    # Linux packman ships no cmake and the box has none — standalone Kitware
    # binary into /tmp (guarded like everything else here)
    if ! command -v cmake >/dev/null && [ ! -x /tmp/cmake-3.30/bin/cmake ]; then
      echo "== fetching standalone cmake =="
      wget -q -O "$DL/cmake.tgz.part" \
        "https://github.com/Kitware/CMake/releases/download/v3.30.5/cmake-3.30.5-linux-x86_64.tar.gz"
      mv "$DL/cmake.tgz.part" "$DL/cmake.tgz"
      mkdir -p /tmp/cmake-3.30
      tar -C /tmp/cmake-3.30 --strip-components=1 -xzf "$DL/cmake.tgz"
    fi
    [ -x /tmp/cmake-3.30/bin/cmake ] && export PATH="/tmp/cmake-3.30/bin:$PATH"
    export PM_CUDA_PATH=/tmp/cuda-13.0
    export PM_PACKAGES_ROOT=/tmp/packman-repo   # test-box $HOME is tiny
    (cd /tmp/physx-src/physx && ./generate_projects.sh linux-gcc)
    # generator is "Unix Makefiles"; the box has no system cmake (packman's
    # copy is only used for the generate step)
    make -C /tmp/physx-src/physx/compiler/linux-gcc-release -j"$(nproc)"
    echo "== assembling /tmp/physx-5.8 prefix =="
    rm -rf /tmp/physx-5.8
    mkdir -p /tmp/physx-5.8/lib /tmp/physx-5.8/bin
    cp -r /tmp/physx-src/physx/include /tmp/physx-5.8/include
    cp /tmp/physx-src/physx/bin/linux.*/release/*_static_64.a /tmp/physx-5.8/lib/
    cp /tmp/physx-src/physx/bin/linux.*/release/libPhysXGpu_64.so /tmp/physx-5.8/bin/
    echo "== caching PhysX prefix to NFS =="
    tar -C /tmp -czf "$PHYSX_TARBALL.part" physx-5.8
    mv "$PHYSX_TARBALL.part" "$PHYSX_TARBALL"
  fi
fi

mkdir -p /tmp/sundog-build

echo "== sanity =="
/tmp/cuda-13.0/bin/nvcc --version | tail -1
ls /tmp/optix-9.1.0/include/optix.h
ls /tmp/physx-5.8/lib/libPhysX_static_64.a /tmp/physx-5.8/bin/libPhysXGpu_64.so
echo "setup-testbox OK"
