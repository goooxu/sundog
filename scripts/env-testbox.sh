# source this on the test box
export CUDA_HOME=/tmp/cuda-13.0
export OPTIX_HOME=/tmp/optix-9.1.0
export PHYSX_HOME=/tmp/physx-5.8
export PATH="$CUDA_HOME/bin:$PATH"
export LD_LIBRARY_PATH="$PHYSX_HOME/bin${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export SUNDOG_BUILD=/tmp/sundog-build
