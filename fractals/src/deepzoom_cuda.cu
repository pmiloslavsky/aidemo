// GPU perturbation for deep zoom: the same per-pixel loop as deep::render's
// CPU version (deepzoom.cpp), one GPU thread per pixel, reading the reference
// orbit from device memory.
#include "deepzoom.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>

using namespace std;

namespace {

__global__ void deep_kernel(EscapeResult *out, unsigned int n, unsigned int h, unsigned int col0,
                            unsigned int w, EscapeParams p, const double *Zr, const double *Zi,
                            unsigned int len) {
  unsigned int k = threadIdx.x + blockDim.x * blockIdx.x;
  if (k >= n) return;
  unsigned int i = col0 + k / h;
  unsigned int j = k % h;
  double dcr = ((double)i - w / 2.0) * p.xdelta;
  double dci = ((double)j - h / 2.0) * p.ydelta;
  double escape = p.escape_r * p.escape_r;

  double dzr = p.julia ? dcr : 0.0, dzi = p.julia ? dci : 0.0;
  unsigned int m = 0;
  double zr = Zr[0] + dzr, zi = Zi[0] + dzi;
  double derr = p.light_r, deri = p.light_i;
  double disti = 0, distr = 0;
  unsigned int iter = 0;

  while (hypot(zr, zi) < escape && iter <= p.iters_max) {
    if (p.shadow_map && !p.julia) {
      double r = 2 * (derr * zr - deri * zi) + p.light_r;
      double im = 2 * (derr * zi + deri * zr) + p.light_i;
      derr = r;
      deri = im;
    }
    double tr = 2 * (Zr[m] * dzr - Zi[m] * dzi) + (dzr * dzr - dzi * dzi);
    double ti = 2 * (Zr[m] * dzi + Zi[m] * dzr) + 2 * dzr * dzi;
    if (!p.julia) {
      tr += dcr;
      ti += dci;
    }
    dzr = tr;
    dzi = ti;
    ++m;
    double nr = Zr[m] + dzr, ni = Zi[m] + dzi;
    distr += (zr - nr) * (zr - nr);
    disti += (zi - ni) * (zi - ni);
    zr = nr;
    zi = ni;
    ++iter;
    if (m + 1 >= len || hypot(zr, zi) < hypot(dzr, dzi)) {
      dzr = zr - Zr[0];
      dzi = zi - Zi[0];
      m = 0;
    }
  }
  EscapeResult r;
  r.z_r = zr;
  r.z_i = zi;
  r.d_r = derr;
  r.d_i = deri;
  r.dist_i = disti;
  r.dist_r = distr;
  r.iter = iter;
  out[k] = r;
}

cudaError_t logError(cudaError_t code, const char *what) {
  cerr << "CUDA error " << (int)code << " (" << cudaGetErrorString(code) << ") in deep zoom "
       << what << endl;
  return code;
}

// The reference orbit on the GPU, shared by the render threads and freed when
// the last one using it is done (the orbit changes when the view moves)
struct DeviceRef {
  double *zr = nullptr, *zi = nullptr;
  unsigned int len = 0;
  ~DeviceRef() {
    cudaFree(zr);
    cudaFree(zi);
  }
};
mutex device_ref_mutex;
unsigned long long device_ref_id = 0;
shared_ptr<DeviceRef> device_ref;

shared_ptr<DeviceRef> upload(const deep::Reference &ref, cudaError_t &err) {
  lock_guard<mutex> lock(device_ref_mutex);
  err = cudaSuccess;
  if (device_ref && device_ref_id == ref.id) return device_ref;
  auto d = make_shared<DeviceRef>();
  size_t bytes = ref.zr.size() * sizeof(double);
  if ((err = cudaMalloc(&d->zr, bytes)) != cudaSuccess) return nullptr;
  if ((err = cudaMalloc(&d->zi, bytes)) != cudaSuccess) return nullptr;
  if ((err = cudaMemcpy(d->zr, ref.zr.data(), bytes, cudaMemcpyHostToDevice)) != cudaSuccess)
    return nullptr;
  if ((err = cudaMemcpy(d->zi, ref.zi.data(), bytes, cudaMemcpyHostToDevice)) != cudaSuccess)
    return nullptr;
  d->len = (unsigned int)ref.zr.size();
  device_ref = d;
  device_ref_id = ref.id;
  return d;
}

atomic<double> steps_per_ms{0};
const double TARGET_MS = 400.0;  // well under Windows' ~2 s GPU timeout

struct Buffer {
  EscapeResult *data = nullptr;
  size_t capacity = 0;
};
thread_local Buffer buffer;

}  // namespace

int deep_render_gpu(const EscapeParams &p, const deep::Reference &ref, unsigned int w,
                    unsigned int h, unsigned int x0, unsigned int x1,
                    std::vector<EscapeResult> &out, const bool *p_reset) {
  cudaError_t err;
  shared_ptr<DeviceRef> d = upload(ref, err);
  if (!d) return (int)logError(err, "reference upload");

  size_t total = (size_t)(x1 - x0) * h;
  out.resize(total);
  if (buffer.capacity < total) {
    cudaFree(buffer.data);
    buffer.data = nullptr;
    buffer.capacity = 0;
    if ((err = cudaMalloc(&buffer.data, total * sizeof(EscapeResult))) != cudaSuccess)
      return (int)logError(err, "cudaMalloc");
    buffer.capacity = total;
  }

  // Column tiles sized for the worst case (every pixel to max iterations)
  const double worst_per_column = (double)h * ((double)p.iters_max + 1);
  unsigned int col = x0;
  while (col < x1) {
    if (p_reset && *p_reset) return CUDA_ESCAPE_RESET;
    double rate = steps_per_ms.load();
    unsigned int cols = 1;
    if (rate > 0)
      cols = (unsigned int)std::clamp(TARGET_MS * rate / worst_per_column, 1.0, (double)(x1 - col));
    unsigned int n = cols * h;
    size_t offset = (size_t)(col - x0) * h;

    auto start = chrono::steady_clock::now();
    deep_kernel<<<(n + 255) / 256, 256>>>(buffer.data + offset, n, h, col, w, p, d->zr, d->zi,
                                          d->len);
    err = cudaGetLastError();
    if (err == cudaSuccess) err = cudaStreamSynchronize(cudaStreamPerThread);
    if (err != cudaSuccess) return (int)logError(err, "kernel");
    double ms = std::max(0.01, chrono::duration<double, milli>(chrono::steady_clock::now() - start).count());
    err = cudaMemcpy(out.data() + offset, buffer.data + offset, n * sizeof(EscapeResult),
                     cudaMemcpyDeviceToHost);
    if (err != cudaSuccess) return (int)logError(err, "cudaMemcpy");

    double steps = 0;
    for (size_t k = offset; k < offset + n; ++k) steps += out[k].iter;
    steps_per_ms.store(std::max(1.0, steps) / ms);
    col += cols;
  }
  return 0;
}
