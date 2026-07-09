# sundog build. Source lives on NFS; on the test box set SUNDOG_BUILD=/tmp/...
# so objects/binaries land on local NVMe.
CUDA_HOME  ?= /tmp/cuda-13.0
OPTIX_HOME ?= /tmp/optix-9.1.0
BUILD      ?= $(if $(SUNDOG_BUILD),$(SUNDOG_BUILD),build)
NVCC       := $(CUDA_HOME)/bin/nvcc
BIN2C      := $(CUDA_HOME)/bin/bin2c
CXX        ?= g++

# IR: 0 = PTX (default), 1 = OptiX-IR.
# nvcc 13.0 --optix-ir output is rejected by the R610 driver's OptiX loader
# (empty COMPILE ERROR); PTX JITs fine and the final ISA is identical.
ARCH  ?= sm_120
IR    ?= 0
DEBUG ?= 0

CXXFLAGS := -std=c++17 -O2 -g -Wall -Wextra -MMD -MP -DSD_HAVE_CUDA \
            -Isrc -Idevice -Iextern -I$(OPTIX_HOME)/include -I$(CUDA_HOME)/include
LDFLAGS  := -L$(CUDA_HOME)/lib64 -lcudart -ldl -lpthread -Wl,-rpath,$(CUDA_HOME)/lib64
NVCCFLAGS := -std=c++17 --use_fast_math -lineinfo \
             -Idevice -I$(OPTIX_HOME)/include

ifeq ($(IR),1)
  DEVFLAG := --optix-ir -arch=$(ARCH)
  DEVEXT  := optixir
else
  DEVFLAG := -ptx -arch=compute_120
  DEVEXT  := ptx
endif
ifeq ($(DEBUG),1)
  CXXFLAGS  := $(filter-out -O2,$(CXXFLAGS)) -O0 -DSUNDOG_DEBUG=1
  NVCCFLAGS := $(filter-out --use_fast_math,$(NVCCFLAGS)) -G
endif

HOST_SRCS := $(wildcard src/*.cpp)
HOST_OBJS := $(HOST_SRCS:src/%.cpp=$(BUILD)/%.o)

# Host-only tests: plain g++, no CUDA/OptiX includes -> run on the dev box too.
TEST_SRCS := $(wildcard tests/host/*.cpp)
TEST_BINS := $(TEST_SRCS:tests/host/%.cpp=$(BUILD)/tests/%)
TESTFLAGS := -std=c++17 -O2 -g -Wall -Idevice -Iextern -Isrc

all: $(BUILD)/sundog

$(BUILD) $(BUILD)/tests:
	mkdir -p $@

# device code -> OptiX-IR/PTX -> embedded C array
$(BUILD)/programs.$(DEVEXT): device/programs.cu $(wildcard device/*.cuh) device/params.h | $(BUILD)
	$(NVCC) $(NVCCFLAGS) $(DEVFLAG) -o $@ $<

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
