# sundog build. Source lives on NFS; on the test box set SUNDOG_BUILD=/tmp/...
# so objects/binaries land on local NVMe.
CUDA_HOME  ?= /tmp/cuda-13.0
OPTIX_HOME ?= /tmp/optix-9.1.0
PHYSX_HOME ?= /tmp/physx-5.8
BUILD      ?= $(if $(SUNDOG_BUILD),$(SUNDOG_BUILD),build)
NVCC       := $(CUDA_HOME)/bin/nvcc
BIN2C      := $(CUDA_HOME)/bin/bin2c
CXX        ?= g++

# Device code ships as PTX (driver JIT): nvcc 13.0's --optix-ir output is
# rejected by current drivers' OptiX loader — full story in the report, ch09.
ARCH  ?= sm_120
DEBUG ?= 0

CXXFLAGS := -std=c++17 -O2 -g -Wall -Wextra -MMD -MP \
            -Isrc -Idevice -Iextern -I$(OPTIX_HOME)/include -I$(CUDA_HOME)/include \
            -I$(PHYSX_HOME)/include
# PhysX static archives must precede -ldl/-lpthread; libPhysXGpu_64.so is
# dlopen'ed at runtime from $(PHYSX_HOME)/bin (rpath below + LD_LIBRARY_PATH
# in scripts/env-testbox.sh).
PHYSX_LIBS := -L$(PHYSX_HOME)/lib \
              -lPhysXExtensions_static_64 -lPhysX_static_64 -lPhysXPvdSDK_static_64 \
              -lPhysXCooking_static_64 -lPhysXCommon_static_64 -lPhysXFoundation_static_64
LDFLAGS  := -L$(CUDA_HOME)/lib64 -lcudart $(PHYSX_LIBS) -ldl -lpthread \
            -Wl,-rpath,$(CUDA_HOME)/lib64 -Wl,-rpath,$(PHYSX_HOME)/bin
NVCCFLAGS := -std=c++17 --use_fast_math -lineinfo \
             -Idevice -I$(OPTIX_HOME)/include

DEVFLAG := -ptx -arch=compute_120
DEVEXT  := ptx
ifeq ($(DEBUG),1)
  CXXFLAGS  := $(filter-out -O2,$(CXXFLAGS)) -O0
  NVCCFLAGS := $(filter-out --use_fast_math,$(NVCCFLAGS)) -G
endif

HOST_SRCS := $(wildcard src/*.cpp)
HOST_OBJS := $(HOST_SRCS:src/%.cpp=$(BUILD)/%.o)

# Host tests: scene parsing/loading only (rendering is GPU-only and is
# validated by golden images + determinism). Needs CUDA/OptiX headers to
# compile, but no GPU to run.
TEST_SRCS := $(wildcard tests/host/*.cpp)
TEST_BINS := $(TEST_SRCS:tests/host/%.cpp=$(BUILD)/tests/%)
TESTFLAGS := -std=c++17 -O2 -g -Wall -Idevice -Iextern -Isrc \
             -I$(OPTIX_HOME)/include -I$(CUDA_HOME)/include

all: $(BUILD)/sundog

$(BUILD) $(BUILD)/tests:
	mkdir -p $@

# device code -> PTX -> embedded C array
#
# CUDA 13 nvcc appends a bare `ptxas` verification pass after emitting PTX.
# OptiX device code calls extern intrinsics (_optix_*) that ptxas cannot
# resolve, so nvcc exits nonzero even though the emitted PTX is complete and
# valid for the OptiX loader. For the PTX path we tolerate the exit code and
# verify completeness ourselves (all 8 program entry points + closing brace);
# a real compile error leaves no/partial PTX and still fails the build.
$(BUILD)/programs.$(DEVEXT): device/programs.cu $(wildcard device/*.cuh) device/params.h | $(BUILD)
	@rm -f $@
	-@$(NVCC) $(NVCCFLAGS) $(DEVFLAG) -o $@ $< 2> $(BUILD)/nvcc-ptx.log || true
	@entries=$$(grep -c '\.visible \.entry' $@ 2>/dev/null || echo 0); \
	last=$$(tail -c 3 $@ 2>/dev/null | tr -d '[:space:]'); \
	if [ "$$entries" -lt 8 ] || [ "$$last" != "}" ]; then \
	  echo "== device PTX incomplete ($$entries entries) — real compile error =="; \
	  cat $(BUILD)/nvcc-ptx.log; rm -f $@; exit 1; \
	fi; \
	grep -v 'ptxas.*_optix_\|ptxas fatal' $(BUILD)/nvcc-ptx.log | grep -v '^$$' | head -5 || true; \
	echo "  [ptx] $@ OK ($$entries entries; nvcc ptxas-verify failure ignored)"

$(BUILD)/embedded_module.c: $(BUILD)/programs.$(DEVEXT)
	$(BIN2C) -c --padd 0 --type char --name g_sundog_module $< > $@
	printf '\nconst unsigned long g_sundog_module_size = sizeof(g_sundog_module);\n' >> $@

# compile as C: in C++ a const array at namespace scope gets internal linkage
$(BUILD)/embedded_module.o: $(BUILD)/embedded_module.c
	$(CC) -c -O1 -o $@ $<

$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/sundog: $(HOST_OBJS) $(BUILD)/embedded_module.o
	$(CXX) -o $@ $^ $(LDFLAGS)

# tests #include device headers and src/scene_json.cpp directly — depend on
# them so edits trigger rebuilds
$(BUILD)/tests/%: tests/host/%.cpp tests/host/check.h $(wildcard device/*.cuh) device/params.h $(wildcard src/*.cpp) $(wildcard src/*.h) | $(BUILD)/tests
	$(CXX) $(TESTFLAGS) -o $@ $<

$(BUILD)/img_compare: tests/tools/img_compare.cpp | $(BUILD)
	$(CXX) $(TESTFLAGS) -o $@ $< -lm

host-tests: $(TEST_BINS)
	@for t in $(TEST_BINS); do echo "== $$t"; $$t || exit 1; done
	@echo "host-tests OK"

smoke: all
	scripts/run-smoke.sh

golden: all $(BUILD)/img_compare
	scripts/run-golden.sh

check: host-tests smoke golden

clean:
	rm -rf $(BUILD)

-include $(BUILD)/*.d

.PHONY: all host-tests smoke golden check clean
