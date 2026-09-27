#include "buddha_cuda_kernel.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>

#include <curand_kernel.h>
#include <cuComplex.h>

//emacs M-X c++-mode

using namespace std;

// Logs a failed CUDA call. Errors are returned to the caller, which switches
// the app to CPU threads for the rest of the session; nothing here exits.
static cudaError_t logCudaError(cudaError_t code, const char *call, const char *file, int line) {
  cerr << "CUDA error " << (int)code << " (" << cudaGetErrorString(code) << ") from " << call
       << " at " << file << ":" << line << endl;
  return code;
}

#define CUDA_TRY(call)                                                     \
  do {                                                                     \
    cudaError_t err_ = (call);                                             \
    if (err_ != cudaSuccess) return logCudaError(err_, #call, __FILE__, __LINE__); \
  } while (0)

static string cudaVersionString(int v) {
  return to_string(v / 1000) + "." + to_string((v % 1000) / 10);
}

//Helper functions for buddhabrot

// z = z^2 + c from z = 0: the number of steps until |z| >= 2, or iters_max if
// c stays in the set that long
__device__ unsigned int escape_iterations(cuDoubleComplex c, unsigned int iters_max) {
  unsigned int n = 0;
  cuDoubleComplex z = make_cuDoubleComplex(0.0, 0.0);
  while (n < iters_max && cuCabs(z) < 2.0) {
    z = cuCadd(cuCmul(z, z), c);
    ++n;
  }
  return n;
}

// Replays the first len steps of the orbit of c and counts each point in the
// pixel it lands on
__device__ void plot_trail(cuDoubleComplex c, unsigned int len, int w, int h,
                           unsigned long long *p_hits, double minx, double maxx, double miny,
                           double maxy) {
  int max_ix = w * h;
  cuDoubleComplex z = make_cuDoubleComplex(0.0, 0.0);
  for (unsigned int i = 0; i < len; ++i) {
    z = cuCadd(cuCmul(z, z), c);
    // if point is plottable, scale it to be on a pixel and increment the
    // value for the pixel
    if ((cuCreal(z) <= maxx) && (cuCreal(z) >= minx) && (cuCimag(z) <= maxy) &&
        (cuCimag(z) >= miny)) {
      //depending on the cast here you might get a faint gridline in your image
      //so be careful
      int x = ((cuCreal(z) - minx) * w) / (maxx - minx);
      int y = ((cuCimag(z) - miny) * h) / (maxy - miny);

      int ix = x + y * w;

      //check for overrun
      if (ix < max_ix) atomicAdd(&p_hits[ix], 1ULL);
    }
  }
}

// Plots the orbit of c if it escapes. Iterating twice (test, then plot)
// instead of storing the orbit keeps per-thread memory tiny and puts no limit
// on the iteration count. Returns true if c escaped.
__device__ bool add_trail(cuDoubleComplex c, unsigned int iters_max, int w, int h,
                          unsigned long long *p_hits, double minx, double maxx, double miny,
                          double maxy, cuda_kernel_stats &stats) {
  unsigned int n = escape_iterations(c, iters_max);
  // If point is in the set we wont use it to color
  if (n == iters_max) {
    ++stats.in_set;
    return false;
  }
  ++stats.escaped_set;
  plot_trail(c, n, w, h, p_hits, minx, maxx, miny, maxy);
  return true;
}

__device__ bool skipInSet_cuda(cuDoubleComplex sample) {
  // if ((abs(sample - complex<double>(-1, 0)) < 0.25) ||
  //     (abs(1.0 - sqrt(1.0 - 4.0 * sample))) < 1.0)
  //Need equivalent math in cuda TODO missing sqrt
  if (cuCabs(cuCsub(sample, make_cuDoubleComplex(-1, 0))) < 0.25) return true;
  return false;
}

//The actual kernel our fractals program uses
//2D array on device is one block of memory
__global__ void generate_hits_kernel(unsigned long long *rH, unsigned long long *gH,
                                     unsigned long long *bH, unsigned long long *p_stats,
                                     int w, int h, double minx, double maxx, double miny,
                                     double maxy, unsigned int red_max, unsigned int green_max,
                                     unsigned int blue_max, unsigned int samples,
                                     unsigned long long seed) {
  int i = threadIdx.x + blockDim.x * blockIdx.x;

  cuda_kernel_stats local_stats = {0, 0, 0, 0};

  curandState state;
  curand_init(seed + i, 0, 0, &state);

  for (unsigned int sample_ix = 0; sample_ix < samples; ++sample_ix) {
    double pr = curand_uniform_double(&state);
    double pi = curand_uniform_double(&state);

    local_stats.total++;

    //scale the random sample
    pr = minx + pr * (maxx - minx);
    pi = miny + pi * (maxy - miny);

    cuDoubleComplex sample = make_cuDoubleComplex(pr, pi);

    if (true == skipInSet_cuda(sample)) {
      local_stats.rejected++;
      continue;
    }

    // One pass per color. An escaping sample's mirror image (its conjugate)
    // escapes too, so it is plotted as well; the next color then starts from
    // the mirrored sample.
    unsigned long long *hits[3] = {rH, gH, bH};
    unsigned int iters[3] = {red_max, green_max, blue_max};
    for (int color = 0; color < 3; ++color) {
      if (add_trail(sample, iters[color], w, h, hits[color], minx, maxx, miny, maxy,
                    local_stats)) {
        sample = make_cuDoubleComplex(cuCreal(sample), -cuCimag(sample));
        add_trail(sample, iters[color], w, h, hits[color], minx, maxx, miny, maxy, local_stats);
      }
    }
  }

  // now update p_stats for all samples for all kernels
  atomicAdd(&p_stats[0], local_stats.rejected);
  atomicAdd(&p_stats[1], local_stats.in_set);
  atomicAdd(&p_stats[2], local_stats.escaped_set);
  atomicAdd(&p_stats[3], local_stats.total);
}

// ---------------------------------------------------------------------------
// Host side
// ---------------------------------------------------------------------------

static int g_sm_count = 0;  // set by cuda_init

bool cuda_init() {
  int runtime = 0, driver = 0;
  cudaRuntimeGetVersion(&runtime);
  cudaDriverGetVersion(&driver);  // 0 when no NVIDIA driver is installed
  cout << "CUDA: runtime " << cudaVersionString(runtime) << ", driver "
       << (driver ? cudaVersionString(driver) : string("not installed")) << endl;
  if (driver == 0) {
    cout << "CUDA: no NVIDIA driver, using CPU threads" << endl;
    return false;
  }

  int count = 0;
  cudaError_t err = cudaGetDeviceCount(&count);
  if (err == cudaErrorInsufficientDriver) {
    cout << "CUDA: the NVIDIA driver (CUDA " << cudaVersionString(driver)
         << ") is too old for this build (needs CUDA " << runtime / 1000
         << ".x); update the driver to use the GPU. Using CPU threads" << endl;
    return false;
  }
  if (err != cudaSuccess || count == 0) {
    cout << "CUDA: no usable NVIDIA GPU ("
         << (err != cudaSuccess ? cudaGetErrorString(err) : "none found")
         << "), using CPU threads" << endl;
    return false;
  }

  cudaDeviceProp prop;
  err = cudaGetDeviceProperties(&prop, 0);
  if (err != cudaSuccess) {
    logCudaError(err, "cudaGetDeviceProperties", __FILE__, __LINE__);
    return false;
  }
  cout << "CUDA: GPU 0 of " << count << ": " << prop.name << ", compute " << prop.major << "."
       << prop.minor << ", " << prop.totalGlobalMem / (1024 * 1024) << " MB, "
       << prop.multiProcessorCount << " SMs"
       << (prop.kernelExecTimeoutEnabled ? ", display watchdog on" : "") << endl;

  // Fails if the fatbin has no code this GPU can run (older than sm_61)
  cudaFuncAttributes attr;
  err = cudaFuncGetAttributes(&attr, generate_hits_kernel);
  if (err != cudaSuccess) {
    cout << "CUDA: no kernel for compute " << prop.major << "." << prop.minor << " in this build ("
         << cudaGetErrorString(err) << "), using CPU threads" << endl;
    return false;
  }

  g_sm_count = prop.multiProcessorCount;
  return true;
}

namespace {

// Hit buffers stay on the GPU between calls; they're zeroed, not reallocated
struct DeviceBuffers {
  unsigned long long *hits[3] = {nullptr, nullptr, nullptr};
  unsigned long long *stats = nullptr;
  size_t pixels = 0;
} dev;

void freeBuffers() {
  for (auto &p : dev.hits) cudaFree(p), p = nullptr;
  cudaFree(dev.stats);
  dev.stats = nullptr;
  dev.pixels = 0;
}

cudaError_t ensureBuffers(size_t pixels) {
  if (dev.pixels == pixels) return cudaSuccess;
  freeBuffers();
  for (auto &p : dev.hits) CUDA_TRY(cudaMalloc(&p, pixels * sizeof(unsigned long long)));
  CUDA_TRY(cudaMalloc(&dev.stats, 4 * sizeof(unsigned long long)));
  dev.pixels = pixels;
  return cudaSuccess;
}

// Keeps each launch short: Windows resets the GPU if one call runs for ~2 s
// on a GPU that drives a display. The work per launch is budgeted in orbit
// steps, so a change of max iterations is accounted for before the launch.
const double TARGET_LAUNCH_MS = 200.0;
double steps_per_ms = 0;  // measured throughput; 0 until the first launch
unsigned long long launches = 0;

cudaError_t runKernel(unsigned int w, unsigned int h, SupportedFractal &frac, SampleStats &stats,
                      vector<unsigned long long> host[3]) {
  size_t pixels = (size_t)w * h;
  CUDA_TRY(ensureBuffers(pixels));
  for (auto p : dev.hits) CUDA_TRY(cudaMemset(p, 0, pixels * sizeof(unsigned long long)));
  CUDA_TRY(cudaMemset(dev.stats, 0, 4 * sizeof(unsigned long long)));

  // All blocks run at once (one wave), so a launch lasts about as long as one
  // thread's samples; the sample count is what keeps it short.
  const unsigned int threads_per_block = 256;
  const unsigned int blocks = std::max(1, g_sm_count) * 4;
  const double threads = (double)blocks * threads_per_block;
  // Worst case per sample: every color runs to max iterations
  double steps_per_sample = std::max(1.0, (double)frac.current_max_iters[0] +
                                              frac.current_max_iters[1] +
                                              frac.current_max_iters[2]);
  unsigned int samples = 1;
  if (steps_per_ms > 0)
    samples = (unsigned int)std::clamp(TARGET_LAUNCH_MS * steps_per_ms / (threads * steps_per_sample),
                                       1.0, 1000.0);

  auto start = chrono::steady_clock::now();
  unsigned long long seed = (unsigned long long)start.time_since_epoch().count() + launches;
  generate_hits_kernel<<<blocks, threads_per_block>>>(
      dev.hits[0], dev.hits[1], dev.hits[2], dev.stats, (int)w, (int)h, frac.xMinMax[0],
      frac.xMinMax[1], frac.yMinMax[0], frac.yMinMax[1], frac.current_max_iters[0],
      frac.current_max_iters[1], frac.current_max_iters[2], samples, seed);
  CUDA_TRY(cudaGetLastError());
  CUDA_TRY(cudaDeviceSynchronize());
  double ms = std::max(0.01, chrono::duration<double, milli>(chrono::steady_clock::now() - start).count());
  steps_per_ms = threads * samples * steps_per_sample / ms;
  if (launches++ < 3)
    cout << "CUDA Buddhabrot: " << blocks << "x" << threads_per_block << " threads, " << samples
         << " samples each, " << (int)ms << " ms" << endl;
  if (samples == 1 && ms > 1000)
    cout << "CUDA Buddhabrot: one sample per thread took " << (int)ms
         << " ms; with more iterations Windows may reset the GPU (~2 s limit)" << endl;

  for (int c = 0; c < 3; ++c) {
    host[c].resize(pixels);
    CUDA_TRY(cudaMemcpy(host[c].data(), dev.hits[c], pixels * sizeof(unsigned long long),
                        cudaMemcpyDeviceToHost));
  }
  cuda_kernel_stats cuda_stats;
  CUDA_TRY(cudaMemcpy(&cuda_stats, dev.stats, sizeof(cuda_stats), cudaMemcpyDeviceToHost));
  stats.total = cuda_stats.total;
  stats.rejected = cuda_stats.rejected;
  stats.in_set = cuda_stats.in_set;
  stats.escaped_set = cuda_stats.escaped_set;
  return cudaSuccess;
}

}  // namespace

//The main app will have one thread that will:
//1) Check for a future asking for thread termination
//2) Run a short cuda kernel (see TARGET_LAUNCH_MS)
//3) move the resulting Hits to the model under mutex
//4) back to 1)
//input: xyrange of fractal   pixel w and h  color max iterations
//output: stats
int cuda_generate_buddhabrot_hits(unsigned int w, unsigned int h, SupportedFractal &frac,
                                  SampleStats &stats,
                                  vector<vector<long long unsigned int>> &redHits,
                                  vector<vector<long long unsigned int>> &greenHits,
                                  vector<vector<long long unsigned int>> &blueHits) {
  //Zero out passed in hits
  redHits.resize(0);
  greenHits.resize(0);
  blueHits.resize(0);

  //1D arrays of hits for cuda (its not good with C++ 2D vector<vector<>> style)
  vector<unsigned long long> host[3];
  cudaError_t err = runKernel(w, h, frac, stats, host);
  if (err != cudaSuccess) {
    freeBuffers();
    return (int)err;
  }

  //Copy the 3 cuda 1D arrays into the user provided 2D arrays
  vector<vector<long long unsigned int>> *out[3] = {&redHits, &greenHits, &blueHits};
  for (int c = 0; c < 3; ++c) {
    out[c]->resize(w);
    for (auto &v : *out[c]) v.resize(h);
    for (unsigned int i = 0; i < w; ++i)
      for (unsigned int j = 0; j < h; ++j) (*out[c])[i][j] = host[c][i + j * w];
  }
  return 0;
}
