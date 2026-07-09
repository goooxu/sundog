// sundog: error-check macros + tiny device buffer RAII helper.
#ifndef SUNDOG_CUDA_CHECK_H
#define SUNDOG_CUDA_CHECK_H

#include <cuda_runtime.h>
#include <optix.h>
#include <optix_stubs.h>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#define CUDA_CHECK(call)                                                     \
  do {                                                                       \
    cudaError_t err__ = (call);                                              \
    if (err__ != cudaSuccess) {                                              \
      throw std::runtime_error(std::string("CUDA error at " __FILE__ ":") +  \
                               std::to_string(__LINE__) + ": " +             \
                               cudaGetErrorString(err__));                   \
    }                                                                        \
  } while (0)

#define OPTIX_CHECK(call)                                                    \
  do {                                                                       \
    OptixResult res__ = (call);                                              \
    if (res__ != OPTIX_SUCCESS) {                                            \
      throw std::runtime_error(std::string("OptiX error at " __FILE__ ":") + \
                               std::to_string(__LINE__) + ": " +             \
                               optixGetErrorName(res__));                    \
    }                                                                        \
  } while (0)

#define OPTIX_CHECK_LOG(call, log, logSize)                                  \
  do {                                                                       \
    OptixResult res__ = (call);                                              \
    if (res__ != OPTIX_SUCCESS) {                                            \
      throw std::runtime_error(std::string("OptiX error at " __FILE__ ":") + \
                               std::to_string(__LINE__) + ": " +             \
                               optixGetErrorName(res__) + "\nlog: " +        \
                               std::string(log, log + logSize));             \
    }                                                                        \
  } while (0)

namespace sd {

struct CudaBuffer {
  CUdeviceptr ptr = 0;
  size_t size = 0;

  CudaBuffer() = default;
  CudaBuffer(const CudaBuffer&) = delete;
  CudaBuffer& operator=(const CudaBuffer&) = delete;
  CudaBuffer(CudaBuffer&& o) noexcept : ptr(o.ptr), size(o.size) {
    o.ptr = 0;
    o.size = 0;
  }
  CudaBuffer& operator=(CudaBuffer&& o) noexcept {
    free();
    ptr = o.ptr;
    size = o.size;
    o.ptr = 0;
    o.size = 0;
    return *this;
  }
  ~CudaBuffer() { free(); }

  void alloc(size_t n) {
    free();
    CUDA_CHECK(cudaMalloc((void**)&ptr, n));
    size = n;
  }
  void allocZero(size_t n) {
    alloc(n);
    CUDA_CHECK(cudaMemset((void*)ptr, 0, n));
  }
  template <typename T>
  void upload(const std::vector<T>& v) {
    alloc(v.size() * sizeof(T));
    CUDA_CHECK(cudaMemcpy((void*)ptr, v.data(), size, cudaMemcpyHostToDevice));
  }
  template <typename T>
  void download(std::vector<T>& v) const {
    v.resize(size / sizeof(T));
    CUDA_CHECK(cudaMemcpy(v.data(), (void*)ptr, size, cudaMemcpyDeviceToHost));
  }
  void free() {
    if (ptr) cudaFree((void*)ptr);
    ptr = 0;
    size = 0;
  }
  template <typename T = void>
  T* as() const { return reinterpret_cast<T*>(ptr); }
};

}  // namespace sd

#endif
