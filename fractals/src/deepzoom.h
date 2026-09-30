#pragma once
// Deep zoom. Past about zoom 1e-10, double precision can no longer tell
// neighboring pixels apart. Mandelbrot and Julia with z^2 + c then switch to
// perturbation: one reference orbit at the view center is computed in high
// precision (512 bits), and every pixel iterates only its small difference
// from that orbit, in ordinary doubles. Rebasing (Zhuoran's method) restarts a
// pixel from the orbit's beginning when it drifts too far, so there are no
// glitches. Differences in double reach zoom ~1e-300; the 512-bit center
// limits the depth to about 1e-150.
//
// The rest of the app keeps its double view (R.xstart, R.xdelta); this module
// holds the exact center. Pan moves it, zoom keeps it, and the app's doubles
// are refreshed from it.

#include <string>
#include <vector>

#include "buddha_cuda_kernel.h"  // EscapeParams, EscapeResult

namespace deep {

// The view center
void set_center(double x, double y);
bool set_center(const std::string &x, const std::string &y);  // decimal strings from a key
void move_center(double dx, double dy);                        // by an offset in fractal units
double center_x();
double center_y();
std::string center_x_str(int digits = 0);  // 0: enough digits for the current depth
std::string center_y_str(int digits = 0);
void set_depth_hint(double pixel_spacing);  // how many digits center_*_str needs

// Deep mode is worth it when the pixel spacing is tiny next to the coordinates
bool wanted(double pixel_spacing);

// Renders columns [x0, x1) of a w x h picture; pixel (i, j) is
// center + ((i - w/2) * p.xdelta, (j - h/2) * p.ydelta). Uses p.iters_max,
// p.escape_r, p.julia, p.zconst_*, p.shadow_map and p.light_*; p.power must be
// 2. out[(i - x0) * h + j] matches cuda_escape_time's layout. Returns 0, or
// CUDA_ESCAPE_RESET when *p_reset turned true. use_gpu tries the GPU first.
int render(const EscapeParams &p, unsigned int w, unsigned int h, unsigned int x0,
           unsigned int x1, std::vector<EscapeResult> &out, const bool *p_reset, bool use_gpu);

// The reference orbit, as doubles (shared with the GPU code)
struct Reference {
  std::vector<double> zr, zi;  // Z_0 .. Z_n
  unsigned long long id = 0;   // unique per computed orbit (the GPU caches by it)
};

}  // namespace deep

// GPU perturbation (deepzoom_cuda.cu; cuda_stub.cpp without CUDA returns 1).
// Same arguments as deep::render plus the reference orbit; returns 0,
// CUDA_ESCAPE_RESET, or a CUDA error code (already logged).
int deep_render_gpu(const EscapeParams &p, const deep::Reference &ref, unsigned int w,
                    unsigned int h, unsigned int x0, unsigned int x1,
                    std::vector<EscapeResult> &out, const bool *p_reset);
