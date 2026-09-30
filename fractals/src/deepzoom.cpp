#include "deepzoom.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>

#include <boost/multiprecision/cpp_bin_float.hpp>

namespace deep {
namespace {

// About 512 bits: enough for zoom ~1e-150
using hp = boost::multiprecision::number<boost::multiprecision::cpp_bin_float<154>,
                                         boost::multiprecision::et_off>;

std::mutex center_mutex;
hp cx = 0, cy = 0;          // guarded by center_mutex
double depth_spacing = 1;  // last pixel spacing, for center_*_str

// Reference orbit cache: recomputed when the center or the orbit settings change
struct RefKey {
  std::string cx, cy;
  unsigned int iters = 0;
  int julia = 0;
  double zc_r = 0, zc_i = 0, escape = 0;
  bool operator==(const RefKey &o) const {
    return cx == o.cx && cy == o.cy && iters == o.iters && julia == o.julia &&
           zc_r == o.zc_r && zc_i == o.zc_i && escape == o.escape;
  }
};
std::mutex ref_mutex;
RefKey ref_key;
std::shared_ptr<const Reference> ref_orbit;

// Z_0 = 0, Z_(n+1) = Z_n^2 + center (Mandelbrot); Z_0 = center,
// Z_(n+1) = Z_n^2 + zconst (Julia). Stops when the orbit escapes.
std::shared_ptr<const Reference> reference(const EscapeParams &p) {
  hp x, y;
  {
    std::lock_guard<std::mutex> lock(center_mutex);
    x = cx;
    y = cy;
  }
  RefKey key{x.str(0, std::ios_base::scientific), y.str(0, std::ios_base::scientific),
             p.iters_max, p.julia, p.zconst_r, p.zconst_i, p.escape_r};
  std::lock_guard<std::mutex> lock(ref_mutex);  // one thread computes, the others wait
  if (ref_orbit && key == ref_key) return ref_orbit;

  static unsigned long long next_id = 0;
  auto ref = std::make_shared<Reference>();
  ref->id = ++next_id;
  hp zr = p.julia ? x : hp(0), zi = p.julia ? y : hp(0);
  hp ar = p.julia ? hp(p.zconst_r) : x, ai = p.julia ? hp(p.zconst_i) : y;
  double escape = p.escape_r * p.escape_r;
  for (unsigned int n = 0; n <= p.iters_max + 1; ++n) {
    double dr = static_cast<double>(zr), di = static_cast<double>(zi);
    ref->zr.push_back(dr);
    ref->zi.push_back(di);
    // keep at least Z_0 and Z_1: pixel() always reads Z_(m+1)
    if (n >= 1 && std::hypot(dr, di) >= escape) break;
    hp r2 = zr * zr, i2 = zi * zi;
    zi = 2 * zr * zi + ai;
    zr = r2 - i2 + ar;
  }
  ref_key = key;
  ref_orbit = ref;
  return ref;
}

// One pixel: iterate the difference dz from the reference orbit Z.
// z_n = Z_m + dz; dz' = 2 Z_m dz + dz^2 (+ dc for Mandelbrot). Same escape
// rule, derivative and distances as mandelbrot_iterations_to_escape.
EscapeResult pixel(const EscapeParams &p, const Reference &ref, double dcr, double dci) {
  const double *Zr = ref.zr.data(), *Zi = ref.zi.data();
  const size_t len = ref.zr.size();
  const double escape = p.escape_r * p.escape_r;

  double dzr = p.julia ? dcr : 0.0, dzi = p.julia ? dci : 0.0;
  size_t m = 0;
  double zr = Zr[0] + dzr, zi = Zi[0] + dzi;
  double derr = p.light_r, deri = p.light_i;
  double disti = 0, distr = 0;
  unsigned int iter = 0;

  while (std::hypot(zr, zi) < escape && iter <= p.iters_max) {
    if (p.shadow_map && !p.julia) {  // derivative * 2 * z + light
      double r = 2 * (derr * zr - deri * zi) + p.light_r;
      double i = 2 * (derr * zi + deri * zr) + p.light_i;
      derr = r;
      deri = i;
    }
    // dz = 2 Z dz + dz^2 (+ dc)
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
    // Rebase: when z is closer to 0 than dz is, or the reference ended, carry
    // on from the start of the reference orbit with dz = z - Z_0
    if (m + 1 >= len || std::hypot(zr, zi) < std::hypot(dzr, dzi)) {
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
  return r;
}

std::string hp_str(const hp &v, int digits) {
  if (digits <= 0) {
    // enough digits to place a pixel, plus a few
    double spacing = std::max(depth_spacing, 1e-300);
    digits = std::max(17, (int)std::ceil(-std::log10(spacing)) + 4);
  }
  return v.str(digits, std::ios_base::fixed);
}

}  // namespace

void set_center(double x, double y) {
  std::lock_guard<std::mutex> lock(center_mutex);
  cx = x;
  cy = y;
}

bool set_center(const std::string &x, const std::string &y) {
  try {
    hp nx(x), ny(y);
    std::lock_guard<std::mutex> lock(center_mutex);
    cx = nx;
    cy = ny;
    return true;
  } catch (...) {
    std::cout << "deep zoom: bad center in key: " << x << ", " << y << std::endl;
    return false;
  }
}

void move_center(double dx, double dy) {
  std::lock_guard<std::mutex> lock(center_mutex);
  cx += dx;
  cy += dy;
}

double center_x() {
  std::lock_guard<std::mutex> lock(center_mutex);
  return static_cast<double>(cx);
}

double center_y() {
  std::lock_guard<std::mutex> lock(center_mutex);
  return static_cast<double>(cy);
}

std::string center_x_str(int digits) {
  std::lock_guard<std::mutex> lock(center_mutex);
  return hp_str(cx, digits);
}

std::string center_y_str(int digits) {
  std::lock_guard<std::mutex> lock(center_mutex);
  return hp_str(cy, digits);
}

void set_depth_hint(double pixel_spacing) {
  std::lock_guard<std::mutex> lock(center_mutex);
  depth_spacing = pixel_spacing;
}

bool wanted(double pixel_spacing) {
  // FRACTALS_DEEP=on/off forces the choice (for comparing the two in tests)
  static const char *force = std::getenv("FRACTALS_DEEP");
  if (force && std::string(force) == "on") return true;
  if (force && std::string(force) == "off") return false;
  double scale = std::max({std::fabs(center_x()), std::fabs(center_y()), 1e-300});
  // Doubles hold ~16 digits; below ~1e-12 of the coordinates, rounding errors
  // grow along the orbit into visible noise before pixels even merge
  return pixel_spacing < 1e-12 * scale;
}

int render(const EscapeParams &p, unsigned int w, unsigned int h, unsigned int x0,
           unsigned int x1, std::vector<EscapeResult> &out, const bool *p_reset, bool use_gpu) {
  std::shared_ptr<const Reference> ref = reference(p);
  if (use_gpu) {
    // 0, reset, or a CUDA error (logged); the app then turns CUDA off
    return deep_render_gpu(p, *ref, w, h, x0, x1, out, p_reset);
  }
  out.resize((size_t)(x1 - x0) * h);
  for (unsigned int i = x0; i < x1; ++i) {
    if (p_reset && *p_reset) return CUDA_ESCAPE_RESET;
    double dcr = ((double)i - w / 2.0) * p.xdelta;
    for (unsigned int j = 0; j < h; ++j) {
      double dci = ((double)j - h / 2.0) * p.ydelta;
      out[(size_t)(i - x0) * h + j] = pixel(p, *ref, dcr, dci);
    }
  }
  return 0;
}

}  // namespace deep
