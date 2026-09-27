#pragma once
#include <vector>
#include "fractals.h"

// Looks for a usable NVIDIA GPU and logs what it finds (runtime and driver
// versions, GPU name). Returns false, after logging why, when there is no
// driver, no GPU, a driver too old for this build, or no kernel for the GPU;
// the app then renders with CPU threads.
bool cuda_init(void);

// The actual nebulabrot implementation: one short GPU launch of random
// samples. Returns 0, or a CUDA error code (already logged) after which the
// caller should stop using CUDA for the session.
int cuda_generate_buddhabrot_hits(unsigned int w, unsigned int h,
                                  SupportedFractal & frac,
				  SampleStats & stats,
                                  std::vector<std::vector<long long unsigned int>> &redHits,
                                  std::vector<std::vector<long long unsigned int>> &greenHits,
                                  std::vector<std::vector<long long unsigned int>> &blueHits);

struct cuda_kernel_stats {
  unsigned long long rejected; // skipInSet check
  unsigned long long in_set;
  unsigned long long escaped_set;
  unsigned long long total;
};

// Escape-time fractals (Mandelbrot, Julia) on the GPU
struct EscapeParams {
  double xstart, ystart, xdelta, ydelta;  // pixel (i, j) is (xstart + i*xdelta, ystart + j*ydelta)
  double power;
  double zconst_r, zconst_i;  // Julia constant
  double escape_r;
  double light_r, light_i;  // shadow map light (the derivative's start value)
  unsigned int iters_max;
  int julia;       // z starts at the pixel and zconst is added, instead of z = 0 and + pixel
  int shadow_map;  // track the derivative for SHADOW_MAP coloring
};

// What the CPU coloring needs for one pixel
struct EscapeResult {
  double z_r, z_i;        // final z
  double d_r, d_i;        // derivative (shadow map)
  double dist_i, dist_r;  // squared distance travelled by the orbit (interior coloring)
  unsigned int iter;      // iterations done
};

const int CUDA_ESCAPE_RESET = -1;

// Computes columns [x0, x1) of a picture h rows high; out[(i - x0) * h + j] is
// pixel (i, j). Works in short tiles and stops early with CUDA_ESCAPE_RESET
// when *p_reset turns true. Returns 0, CUDA_ESCAPE_RESET, or a CUDA error
// code (already logged) after which the caller should stop using CUDA.
int cuda_escape_time(const EscapeParams &p, unsigned int x0, unsigned int x1, unsigned int h,
                     std::vector<EscapeResult> &out, const bool *p_reset);
