#include <math.h>
#include <signal.h>

#include <SFML/Graphics.hpp>
#include <TGUI/TGUI.hpp>
#include <TGUI/Backend/SFML-Graphics.hpp>
#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <climits>
#include <iomanip>
#include <optional>
#include <sstream>
#include <chrono>
#include <cmath>
#include <complex>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>

#include "buddha_cuda_kernel.h"
#include "fractals.h"
#include "runtime.h"
#include "deepzoom.h"
#include "tinycolormap.hpp"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

// Key files store palettes by name. The first entry is also what an unknown name
// maps to, so it is the default palette (UF16).
namespace tinycolormap {
NLOHMANN_JSON_SERIALIZE_ENUM(ColormapType,
                             {{ColormapType::UF16, "UF16"},
                              {ColormapType::Parula, "Parula"},
                              {ColormapType::Heat, "Heat"},
                              {ColormapType::Jet, "Jet"},
                              {ColormapType::Turbo, "Turbo"},
                              {ColormapType::Hot, "Hot"},
                              {ColormapType::Gray, "Gray"},
                              {ColormapType::Magma, "Magma"},
                              {ColormapType::Inferno, "Inferno"},
                              {ColormapType::Plasma, "Plasma"},
                              {ColormapType::Viridis, "Viridis"},
                              {ColormapType::Cividis, "Cividis"},
                              {ColormapType::Github, "Github"},
                              {ColormapType::Cubehelix, "Cubehelix"}})
}  // namespace tinycolormap

// Fractals:
//  Mandlebrot
//  Nebulabrot (3 colors)
//  Mandelbrot with Power parameter
//  Julia with power parameter and start parameter
//  Buddhabrot (one color)
//  Buddha with power (non cuda)
//  Nebulabrot cuda
//  Spiral Septagon
// TODO
// Allow fractal panning by arrow keys
// Mandelbrot coloring improvements
//  Work on smooth coloring and colormaps more
// Orbit Traps: Point, Lines, Shapes, pickover stalk
// Color based on pixel in input image
// Interior coloring
// reset all variables on fractal switch
// "Reflect colormap toggle" - is it actually reflected on gui
// save position in file name
// make font bold black
// memory:
// save position and scale (just x?) and miters and zconst and power to db file
// display saved position database
// apply specified position from database
// apply next position from database (button?)
// display if position in database on gui

using namespace std;
namespace fs = std::filesystem;

// The overall goal is to create some mildly zoomable models
// of fractals where its easy to see interesting regions

// Model:
//  Store 2D vector of checked points for size of zoomed and panned region
//  Gather hits from subthreads for buddhabrot
//

// Visual Elements (View):
// Zoomed region of the fractal
// Gui status elements
// Gui control elements (fractal selection)
//

// Controls:
//  Center via mouse click (mandelbrot only)
//  Zoom via mouse (mandelbrot only)
//  Hide/Show all GUI controls via hotkey
//  screenshot hotkey
//  full screen hotkey
//  cuda on hotkey

void signal_callback_handler(int signum) {
  cout << "Caught signal " << signum << endl;
  // Terminate program
  exit(signum);
}

// Render size: the desktop resolution (set in main), or 2560x1440 for
// --save-and-exit so movie frames and CI renders are reproducible
int IMAGE_WIDTH = 2560;
int IMAGE_HEIGHT = 1440;

// const int IMAGE_WIDTH = 1600;
// const int IMAGE_HEIGHT = 1200;

// The zoom and rotation and panning and color of the fractal

enum class ColoringAlgo { MULTICYCLE, SMOOTH, USE_IMAGE, SHADOW_MAP };

enum class ColorCycle { CC8, CC16, CC32, CC64, CC128, CC256 };

enum class InteriorColoringAlgo {
  SOLID,
  MULTICYCLE,
  USE_IMAGE,
  TRIG,
  DIST,
  DIST2
};

// Names used in key files; the first entry is the default for unknown names
NLOHMANN_JSON_SERIALIZE_ENUM(ColoringAlgo, {{ColoringAlgo::MULTICYCLE, "MULTICYCLE"},
                                            {ColoringAlgo::SMOOTH, "SMOOTH"},
                                            {ColoringAlgo::USE_IMAGE, "USE_IMAGE"},
                                            {ColoringAlgo::SHADOW_MAP, "SHADOW_MAP"}})
NLOHMANN_JSON_SERIALIZE_ENUM(InteriorColoringAlgo,
                             {{InteriorColoringAlgo::SOLID, "SOLID"},
                              {InteriorColoringAlgo::MULTICYCLE, "MULTICYCLE"},
                              {InteriorColoringAlgo::USE_IMAGE, "USE_IMAGE"},
                              {InteriorColoringAlgo::TRIG, "TRIG"},
                              {InteriorColoringAlgo::DIST, "DIST"},
                              {InteriorColoringAlgo::DIST2, "DIST2"}})
unsigned int interior_color_adjust = 0;

// std::is_trivially_copyable
class ReferenceFrame {
 public:
  // x-y rotation angle [future]
  float theta;

  // View
  //
  double xstart;          // in fractal coordinates
  double ystart;          // in fractal coordinates
  double xdelta;          // delta per pixel in fractal coordinates
  double ydelta;          // delta per pixel in fractal coordinates
  double current_width;   // in pixels
  double current_height;  // in pixels

  // Zoom Level
  double displayed_zoom;
  double requested_zoom;

  // zoom crop area
  bool show_selection;

  // Color Palletes
  ColoringAlgo color_algo;
  int color_cycle_size;

  tinycolormap::ColormapType palette;
  bool reflect_palette;

  // Escape Image coloring
  unsigned int escape_image_w;
  unsigned int escape_image_h;
  bool image_loaded;

  // normal maps
  double light_pos_r;
  double light_pos_i;
  double light_angle;
  double light_height;

  // Random Sample faster but less exact rendering
  bool random_sample = true;  // Buddhabrots: random samples (the grid builds up row by row)

  // Mandelbrot/Julia: raise max iterations with the zoom (see effective_iters)
  bool auto_iterations = true;

  // Original total Pixels
  double original_width;
  double original_height;

  ReferenceFrame(float _thetaxy, double _zoom)
      : theta{_thetaxy}, displayed_zoom{_zoom} {};

};  // ReferenceFrame

// Non Serializable part of Fractal description thats too big to store in a key
class NSReferenceFrame {
 public:
  // Band sizes 16 << index (the lists and cycleIndex use the same rule)
  vector<string> color_cycle_size_names{string("16"),  string("32"),  string("64"),
                                        string("128"), string("256"), string("512"),
                                        string("1024")};
  vector<string> color_names{string("Parula"),
                             string("Heat"),
                             string("Jet"),  // same order as tinycolormap::ColormapType
                             string("Turbo"),
                             string("Hot"),
                             string("Gray"),
                             string("Magma"),
                             string("Inferno"),
                             string("Plasma"),
                             string("Viridis"),
                             string("Cividis"),
                             string("Github"),
                             string("Cubehelix"),
                             string("UF16")};
};  // NSReferenceFrame

ReferenceFrame R(0, 1.0);
NSReferenceFrame NSR;  // non serializable

// The image for USE_IMAGE coloring (outside and inside the set). Loading one
// (the n key) swaps in a new, never-modified object. Each render thread takes
// a snapshot for its slice, so an image is never changed or freed while a
// thread reads it; replacing it in place crashed when n was pressed quickly.
struct EscapeImage {
  sf::Image image;
  unsigned int w = 0, h = 0;
};
std::mutex escape_image_mutex;
std::shared_ptr<const EscapeImage> escape_image_current;  // guarded by the mutex
thread_local const EscapeImage *t_escape_image = nullptr;  // this thread's snapshot

std::shared_ptr<const EscapeImage> snapshot_escape_image() {
  std::lock_guard<std::mutex> lock(escape_image_mutex);
  return escape_image_current;
}

bool load_escape_image(const std::string &file) {
  auto img = std::make_shared<EscapeImage>();
  if (!img->image.loadFromFile(file)) return false;
  img->w = img->image.getSize().x;
  img->h = img->image.getSize().y;
  if (img->w == 0 || img->h == 0) return false;
  {
    std::lock_guard<std::mutex> lock(escape_image_mutex);
    escape_image_current = img;
  }
  R.escape_image_w = img->w;
  R.escape_image_h = img->h;
  R.image_loaded = true;
  std::cout << "Loaded escape_image " << file << " Dims: " << img->w << " " << img->h
            << std::endl;
  return true;
}

// The color at (x, y) in [0, 1) of this thread's image; black if there is none
sf::Color escape_image_color(double x, double y) {
  const EscapeImage *img = t_escape_image;
  if (!img) return sf::Color::Black;
  auto index = [](double v, unsigned int n) {
    return v >= 0 ? (unsigned int)std::min(v, (double)(n - 1)) : 0u;  // also catches NaN
  };
  return img->image.getPixel(
      sf::Vector2u(index(x * (img->w - 1), img->w), index(y * (img->h - 1), img->h)));
}


class ReferenceFrameInt {
 public:
  // Color Palletes
  InteriorColoringAlgo color_algo;
  int color_cycle_size;

  tinycolormap::ColormapType palette;
  bool reflect_palette;
  ReferenceFrameInt(InteriorColoringAlgo _ca, int _ccs, tinycolormap::ColormapType _p, bool _rp)
      : color_algo{_ca},
        color_cycle_size{_ccs},
        palette{_p},
        reflect_palette{_rp} {};
};  // ReferenceFrameInt

ReferenceFrameInt RI(InteriorColoringAlgo::SOLID, 256,
                     tinycolormap::ColormapType::UF16, false);


// Submodel for different fractals
// #include "fractals.h" SupportedFractal (shared with cuda)

// struct SupportedFractal {
//   std::string name;
//   bool cuda_mode;
//   bool probabalistic; //i.e. like buddha - affects thread model
//   bool julia;
//   bool anti;
//   std::vector<double> xMinMax;  // default x min max
//   std::vector<double> yMinMax;  // default min max
//   std::vector<unsigned int> current_max_iters;
//   std::vector<unsigned int> default_max_iters;
//   double current_power;
//   double default_power;
//   std::complex<double> current_zconst;
//   std::complex<double> default_zconst;
// };

vector<SupportedFractal> FRAC = {
    {string("Mandelbrot_300"),
     true,  // cuda_mode: GPU when available (M6: same image, faster)
     false,
     false,  // julia
     false,
     {-2.50, 1.5},
     {-1.4, 1.4},
     {300, 0, 0},
     {300, 0, 0},
     2,
     2,
     complex<double>{0, 0},
     complex<double>{0, 0},
     2,
     2},
    {string("Mandelbrot_1000"),
     true,  // cuda_mode: GPU when available (M6: same image, faster)
     false,
     false,  // julia
     false,
     {-2.5, 1.5},
     {-1.4, 1.4},
     {1000, 0, 0},
     {1000, 0, 0},
     2,
     2,
     complex<double>{0, 0},
     complex<double>{0, 0},
     2,
     2},
    {string("Julia"),
     true,  // cuda_mode: GPU when available (M6: same image, faster)
     false,
     true,  // julia
     false,
     {-2.2, 2.2},
     {-1.2, 1.2},
     {300, 0, 0},
     {300, 0, 0},
     2,
     2,
     complex<double>{-0.79, 0.15},
     complex<double>{-0.79, 0.15},
     2,
     2},
    {string("Spiral_Septagon"),
     false,
     false,
     false,
     false,
     {-2.2, 2.2},
     {-1.4, 1.4},
     {300, 0, 0},
     {300, 0, 0},
     7,
     7,
     complex<double>{0, 0},
     complex<double>{0, 0},
     2,
     2},
    {string("Buddhabrot"),  // not going to be zoomable and pannable
     true,
     true,
     false,
     false,
     {-2.2, 1.0},
     {-1.2, 1.2},
     {10000, 1000, 100},
     {10000, 1000, 100},
     2,
     2,
     complex<double>{0, 0},
     complex<double>{0, 0},
     2,
     2},
    {string("Buddhabrot_BW"),  // not going to be zoomable and pannable
     true,
     true,
     false,
     false,
     {-2.2, 1.0},
     {-1.2, 1.2},
     {10000, 10000, 10000},
     {10000, 10000, 10000},
     2,
     2,
     complex<double>{0, 0},
     complex<double>{0, 0},
     2,
     2},
    {string("Buddhabrot_General"),  // not going to be zoomable and pannable
     false,
     true,
     false,
     false,
     {-2.2, 1.0},
     {-1.2, 1.2},
     {10000, 1000, 100},
     {10000, 1000, 100},
     2,
     2,
     complex<double>{0, 0},
     complex<double>{0, 0},
     2,
     2},
    {string(
         "Buddhabrot_General_Julia"),  // not going to be zoomable and pannable
     false,
     true,
     true,  // julia
     false,
     {-2.2, 1.0},
     {-1.2, 1.2},
     {10000, 1000, 100},
     {10000, 1000, 100},
     2,
     2,
     complex<double>{0, 0},
     complex<double>{0, 0},
     2,
     2},
    {string(
         "Anti_Buddhabrot_General"),  // not going to be zoomable and pannable
     false,
     true,
     false,
     true,  // anti
     {-2.2, 1.0},
     {-1.2, 1.2},
     {10000, 10000, 10000},
     {10000, 10000, 10000},
     2,
     2,
     complex<double>{0, 0},
     complex<double>{0, 0},
     2,
     2},
    {string("Anti_Buddhabrot_Small"),  // not going to be zoomable and pannable
     false,
     true,
     false,
     true,  // anti
     {-1.5, .25},
     {-1.0, .25},
     {10000, 10000, 10000},
     {10000, 10000, 10000},
     2,
     2,
     complex<double>{0, 0},
     complex<double>{0, 0},
     2,
     2},
    {string("Nova_z6+z3-1"),
     false,
     false,
     false,
     false,
     {-2.5, 1.5},
     {-1.4, 1.4},
     {300, 0, 0},
     {300, 0, 0},
     6,
     6,
     complex<double>{0, 0},
     complex<double>{0, 0},
     2,
     2},
    {string("Newton_z6+z3-1"),
     false,
     false,
     false,
     false,
     {-2.5, 1.5},
     {-1.4, 1.4},
     {300, 0, 0},
     {300, 0, 0},
     6,
     6,
     complex<double>{0, 0},
     complex<double>{0, 0},
     2,
     2},
};


// std::filesystem::path::preferred_separator
//  / works on windows also
std::string separator{"/"};

const int FRACTAL_VERSION{1};

// FractalsData/ (see runtime.h); set in main before anything loads or saves
fs::path data_dir;
std::string keys_location;

// A fractal description, kept in memory for undo and "Save Fractal", and
// written to disk as a JSON key (see keyToJson)
class SavedFractal {
 public:
  int version = FRACTAL_VERSION;
  // p_model->current_fractal
  // FRAC[p_model->current_fractal].current_power
  // FRAC[p_model->current_fractal].current_max_iters[0]
  // FRAC[p_model->current_fractal].current_zconst
  int valid;
  unsigned int current_fractal;
  unsigned int current_max_iters[3];
  double current_power;
  std::complex<double> current_zconst;
  double current_escape_r;

  ReferenceFrame RF;
  ReferenceFrameInt RI;  // interior coloring
  std::string center_x, center_y;  // exact view center (deep zoom); may be empty
  // but not NSR

  SavedFractal(float _thetaxy, double _zoom)
      : valid{0},
        RF{_thetaxy, _zoom},
        RI{InteriorColoringAlgo::SOLID, 256, tinycolormap::ColormapType::UF16, false} {};
};

//#include "fractals.h" SampleStats
// struct SampleStats {
//   unsigned long long rejected; // skipInSet check
//   unsigned long long in_set;
//   unsigned long long escaped_set;
//   unsigned long long total;
//   //stats for thread efficiency and total thread progress
//   unsigned long long samples_per_second; //i.e. "total" - not hits
//   std::chrono::time_point<chrono::steady_clock> next_second_start;
//   unsigned long long samples_last_second;
// };

// Palette lookups. tinycolormap turns NaN into an out-of-range table index
// (undefined behavior; on x64 it happens to give black). NaN comes from
// log(log2|z|) when an orbit escapes with |z| < 1 (Newton, Nova, a small escape
// radius) or from dividing by 0 max iterations, so NaN maps to the palette start.
inline tinycolormap::Color palette_color(double x, tinycolormap::ColormapType type) {
  return tinycolormap::GetColor(std::isnan(x) ? 0.0 : x, type);
}
inline tinycolormap::Color palette_color_r(double x, tinycolormap::ColormapType type) {
  return tinycolormap::GetColorR(std::isnan(x) ? 0.0 : x, type);
}

// Ultra Fractal's default palette (UF16). A constant table: building it per
// pixel (17 heap allocations) made the render threads fight over the heap.
static const int UF16_MAPPING[16][3] = {
    {66, 30, 15},    {25, 7, 26},     {9, 1, 47},      {4, 4, 73},
    {0, 7, 100},     {12, 44, 138},   {24, 82, 177},   {57, 125, 209},
    {134, 181, 229}, {211, 236, 248}, {241, 233, 191}, {248, 201, 95},
    {255, 170, 0},   {204, 128, 0},   {153, 87, 0},    {106, 52, 3}};

inline void get_iteration_color(const int iter_ix, const int iters_max,
                                const complex<double> &zfinal,
                                complex<double> &derivative, int *p_rcolor,
                                int *p_gcolor, int *p_bcolor) {
  // palette: UF16, Viridis, Plasma, Jet, Hot, Heat, Parula, Gray, Cividis,
  // Github, UF16(added) cycle_size: 8,16,32,64,128,256 color_algo: Smooth,
  // MultiCycle

  if (R.color_algo == ColoringAlgo::USE_IMAGE) {
    double rd, ri;
    double xi, yi;
    xi = abs(modf(zfinal.real() * 2, &rd));
    yi = abs(modf(zfinal.imag() * 2, &ri));
    // xi = abs(zfinal.real() - (long long)zfinal.real());
    // yi = abs(zfinal.imag() - (long long)zfinal.imag());
    sf::Color color = escape_image_color(xi, yi);
    *p_rcolor = color.r;
    *p_gcolor = color.g;
    *p_bcolor = color.b;
    return;
  } else if (R.color_algo == ColoringAlgo::SHADOW_MAP) {
    const double h2 = R.light_height;  // height factor of the incoming light
    const double angle = R.light_angle / 360;  // incoming direction of light
    const complex<double> I(0.0, 1.0);
    complex<double> u;
    double t;
    const double pi = 3.14159265358979323846;
    complex<double> v =
        std::exp(I * complex<double>((angle * 2 * pi),
                                     0));  // unit 2D vector in this direction
    // incoming light 3D vector = (v.re, v.im, h2)

    u = zfinal / derivative;
    u = u / abs(u);  // normal vector : (u.re, u.im, 1)
    t = u.real() * v.real() + u.imag() * v.imag() +
        h2;            // dot product with the incoming light
    t = t / (1 + h2);  // rescale so that t does not get bigger than 1
    if (t < 0) t = 0;
      // a+t(b-a) black -> white
#if 0
		if (p_rcolor != 0) *p_rcolor = t * 255;
		if (p_gcolor != 0) *p_gcolor = t * 255;
		if (p_bcolor != 0) *p_bcolor = t * 255;
#else
    // colormap
    int i = (int)(t * 256) % R.color_cycle_size;
    tinycolormap::Color color(0.0, 0.0, 0.0);
    if (R.reflect_palette)
      color = palette_color_r(
          i / static_cast<double>(R.color_cycle_size), R.palette);
    else
      color = palette_color(
          i / static_cast<double>(R.color_cycle_size), R.palette);

    *p_rcolor = (int)(255 * color.r());
    *p_gcolor = (int)(255 * color.g());
    *p_bcolor = (int)(255 * color.b());
#endif
    return;
  }

  if (R.palette == tinycolormap::ColormapType::UF16) {
    // Ultra Fractal Default non smooth

    int i = iter_ix % 16;
    if (R.reflect_palette) {
      i = iter_ix % 32;
      if (i >= 16) i = 31 - i;
    }

    *p_rcolor = UF16_MAPPING[i][0];
    *p_gcolor = UF16_MAPPING[i][1];
    *p_bcolor = UF16_MAPPING[i][2];
    return;
  }

  // Using tinycolormap

  if (R.color_algo == ColoringAlgo::MULTICYCLE) {
    // colormap non smooth
    int i = iter_ix % R.color_cycle_size;
    tinycolormap::Color color(0.0, 0.0, 0.0);
    if (R.reflect_palette)
      color = palette_color_r(
          i / static_cast<double>(R.color_cycle_size), R.palette);
    else
      color = palette_color(
          i / static_cast<double>(R.color_cycle_size), R.palette);

    *p_rcolor = (int)(255 * color.r());
    *p_gcolor = (int)(255 * color.g());
    *p_bcolor = (int)(255 * color.b());
  } else if (R.color_algo == ColoringAlgo::SMOOTH) {
    double smooth = ((iter_ix + 1 - log(log2(abs(zfinal)))));  // 0 -> iters_max
    tinycolormap::Color color(0.0, 0.0, 0.0);
    if (R.reflect_palette)
      color = palette_color_r(smooth / iters_max, R.palette);
    else
      color = palette_color(smooth / iters_max, R.palette);

    *p_rcolor = (int)(255 * color.r());
    *p_gcolor = (int)(255 * color.g());
    *p_bcolor = (int)(255 * color.b());
  }
  return;

  // smooth but use chunked colormap
  //  double smooth = ((iter_ix + 1 - log(log2(abs(z))))); //0 -> iters_max

  // //colormap portions - chunk colormap into 32
  // double chunked_smooth = std::round(smooth * 8.0)/8.0;

  // const tinycolormap::Color color =
  // tinycolormap::GetColor(chunked_smooth/static_cast<double>(iters_max),
  // tinycolormap::ColormapType::Viridis);

  // *p_rcolor = 255*color.r();
  // *p_gcolor = 255*color.g();
  // *p_bcolor = 255*color.b();

  // Basic coloring:
  //     return (255 * iter_ix) / (iters_max - 1);
  //   smooth coloring ????    mu = N + 1 - log (log  |Z(N)|) / log 2
  // double smooth = ((iter_ix + 1 - log(log2(abs(z))))/iters_max; //0 -> 1

  // *p_rcolor = (255*iter_ix) / (iters_max);
  // *p_gcolor = 255 - (255*iter_ix) / (iters_max);
  // *p_bcolor = clamp(128 + (255*iter_ix) / (iters_max),(unsigned
  // int)0,(unsigned int)255);
}

inline void get_iteration_interior_color(const complex<double> &zstart,
                                         const complex<double> &zfinal,
                                         unsigned int iters_max,
                                         double distancei, double distancer,
                                         int *p_rcolor, int *p_gcolor,
                                         int *p_bcolor) {
  const double pi = 3.14159265358979323846;

  if ((interior_color_adjust == 0) &&
      (RI.color_algo != InteriorColoringAlgo::SOLID))
    interior_color_adjust = 1;

  switch (RI.color_algo) {
    case InteriorColoringAlgo::SOLID: {
      *p_rcolor = interior_color_adjust & 0xff;
      *p_gcolor = (interior_color_adjust & 0xff00) >> 8;
      *p_bcolor = (interior_color_adjust & 0xff0000) >> 16;
      return;
    } break;
    case InteriorColoringAlgo::MULTICYCLE: {
      if (RI.palette == tinycolormap::ColormapType::UF16) {
        // Ultra Fractal Default non smooth

        int i = (interior_color_adjust *10 * (int)(distancer + distancei)) % 16;
        if (RI.reflect_palette) {
          i = (interior_color_adjust * 10 * (int)(distancer + distancei)) % 32;
          if (i >= 16) i = 31 - i;
        }

        *p_rcolor = UF16_MAPPING[i][0];
        *p_gcolor = UF16_MAPPING[i][1];
        *p_bcolor = UF16_MAPPING[i][2];
        return;
      }

      // colormap non smooth
      int i = (interior_color_adjust * (int)(distancer + distancei)) %
              RI.color_cycle_size;
      tinycolormap::Color color(0.0, 0.0, 0.0);
      if (RI.reflect_palette)
        color = palette_color_r(
            i / static_cast<double>(RI.color_cycle_size), RI.palette);
      else
        color = palette_color(
            i / static_cast<double>(RI.color_cycle_size), RI.palette);

      *p_rcolor = (unsigned int)(255 * color.r());
      *p_gcolor = (unsigned int)(255 * color.g());
      *p_bcolor = (unsigned int)(255 * color.b());
      return;
    } break;
    case InteriorColoringAlgo::USE_IMAGE: {
      double rd, ri;
      double xi, yi;
      xi = abs(
          modf(zstart.real() * (2 / (interior_color_adjust*R.displayed_zoom)),
               &rd));  // -1 -> 1
      yi = abs(modf(
          zstart.imag() * (2 / (interior_color_adjust * R.displayed_zoom)), &ri));
      // xi = abs(zstart.real() - (long long)zstart.real());
      // yi = abs(zstart.imag() - (long long)zstart.imag());
      sf::Color color = escape_image_color(xi, yi);
      *p_rcolor = color.r;
      *p_gcolor = color.g;
      *p_bcolor = color.b;
      return;
    } break;
    case InteriorColoringAlgo::TRIG: {
      *p_rcolor = (int)(255 * (cos(zfinal.imag() + zfinal.real())) * 0.1 *
                  ((interior_color_adjust / R.displayed_zoom) *
                   (distancer + distancei))  / (iters_max));
      *p_gcolor = (int)(255 * (sin(zfinal.real() + zfinal.real())) * 0.1 *
                  ((interior_color_adjust / R.displayed_zoom) *
                   (distancer + distancei))  / (iters_max));
      *p_bcolor = (int)(255 * (atan(zfinal.imag() + zfinal.real())) * 0.1 *
                  ((interior_color_adjust / R.displayed_zoom) *
                   (distancer + distancei)) / (iters_max));
      return;
    } break;
    case InteriorColoringAlgo::DIST: {
      *p_rcolor = (int)(interior_color_adjust * 255 * (distancer) / (iters_max));
      *p_gcolor = (int)(interior_color_adjust * 255 * (distancei) / (iters_max));
      *p_bcolor = (int)(interior_color_adjust * 255 * (distancer + distancei) /
                        (iters_max));
      return;
    } break;
    case InteriorColoringAlgo::DIST2: {
      *p_rcolor =
          (int)(255.0 * (cos(distancer + distancei)) *
                ((1.0 / interior_color_adjust) * (distancer + distancei)) /
                (iters_max));
      *p_gcolor =
          (int)(255.0 * (sin(distancer + distancei)) *
                ((1.0 / interior_color_adjust) * (distancer + distancei)) /
                (iters_max));
      *p_bcolor =
          (int)(255.0 * (atan(distancer + distancei)) *
                ((1.0 / interior_color_adjust) * (distancer + distancei)) /
                (iters_max));
      return;
    } break;
  }
}

// z^power for the Mandelbrot/Julia iteration. Integer powers (the usual z^2)
// use plain complex multiplication: exact IEEE arithmetic, so it is more
// accurate at deep zoom than std::pow's exp(p*log z) and gives the same bits
// as the GPU kernel (pow_int in buddha_cuda_kernel.cu). Other powers use std::pow.
inline complex<double> escape_pow(const complex<double> &z, double power) {
  if (power >= 1 && power <= 16 && power == std::floor(power)) {
    double wr = z.real(), wi = z.imag();
    for (int k = 1; k < (int)power; ++k) {
      double r = wr * z.real() - wi * z.imag();
      double i = wr * z.imag() + wi * z.real();
      wr = r;
      wi = i;
    }
    return complex<double>(wr, wi);
  }
  return pow(z, power);
}

// Colors one Mandelbrot/Julia pixel from the end of its orbit. Shared by the
// CPU iteration below and the GPU results (cuda_escape_time).
void color_escape_pixel(const complex<double> &point, unsigned int iter_ix,
                        unsigned int iters_max, complex<double> z,
                        complex<double> derivative, double distancei,
                        double distancer, int *p_rcolor, int *p_gcolor,
                        int *p_bcolor, unsigned long long &in,
                        unsigned long long &out) {
  if (iter_ix < iters_max)
    ++out;
  else
    ++in;

  if (iter_ix < iters_max) {
    get_iteration_color(iter_ix, iters_max, z, derivative, p_rcolor, p_gcolor,
                        p_bcolor);
  } else {  // set interior set color
    get_iteration_interior_color(point, z, iters_max, distancei, distancer,
                                 p_rcolor, p_gcolor, p_bcolor);
  }
}

// Fractals that cuda_escape_time can render: the ones getImagePixels hands to
// mandelbrot_iterations_to_escape
bool has_escape_kernel(const SupportedFractal &f) {
  return !f.probabalistic && f.name != "Spiral_Septagon" &&
         f.name != "Nova_z6+z3-1" && f.name != "Newton_z6+z3-1";
}

// Max iterations actually used. With auto iterations (Mandelbrot and Julia
// only) the box value is a base that grows as you zoom in: points near the
// boundary need more steps to show that they escape, and with too few they
// are counted as inside. x1 at zoom 1, x5 at 1e-4, x14 at 1e-13.
unsigned int effective_iters(const SupportedFractal &f) {
  unsigned int base = f.current_max_iters[0];
  if (!R.auto_iterations || !has_escape_kernel(f) || R.displayed_zoom >= 1.0 ||
      !(R.displayed_zoom > 0))
    return base;
  double factor = 1.0 + std::log10(1.0 / R.displayed_zoom);
  return (unsigned int)std::min(base * factor, 100000000.0);
}

// Whether the current settings of f can run on the GPU. The Buddhabrot kernel
// only does plain z^2 + c, so Julia, anti and other powers stay on the CPU.
bool has_gpu_kernel(const SupportedFractal &f) {
  if (f.probabalistic) return !f.julia && !f.anti && f.current_power == 2;
  return has_escape_kernel(f);
}

void mandelbrot_iterations_to_escape(double x, double y, unsigned int iters_max,
                                     int *p_rcolor, int *p_gcolor,
                                     int *p_bcolor, double power,
                                     complex<double> zconst, double escape_r,
                                     bool julia, unsigned long long &in,
                                     unsigned long long &out) {
  complex<double> point(x, y);
  complex<double> z(0, 0);
  complex<double> zn(0, 0);
  complex<double> dc(R.light_pos_r, R.light_pos_i);
  complex<double> derivative = dc;
  unsigned int iter_ix = 0;
  double distancei = 0;
  double distancer = 0;

  if (julia) z = point;

  while (abs(z) < (escape_r * escape_r) && iter_ix <= iters_max) {
    if (julia)
      zn = escape_pow(z, power) + zconst;  // With Julia you dont add Point
    else {
      if (R.color_algo == ColoringAlgo::SHADOW_MAP)
        derivative =
            derivative * complex<double>(2, 0) * z + dc;  // shadow map only
      zn = escape_pow(z, power) + point;
    }
    // how far did we travel during orbit
    distancei += (z.imag() - zn.imag()) * (z.imag() - zn.imag());
    distancer += (z.real() - zn.real()) * (z.real() - zn.real());
    z = zn;
    // z = z*z + point;
    iter_ix++;
  }

  color_escape_pixel(point, iter_ix, iters_max, z, derivative, distancei,
                     distancer, p_rcolor, p_gcolor, p_bcolor, in, out);
}

void spiral_septagon_iterations_to_escape(
    double x, double y, unsigned int iters_max, int *p_rcolor, int *p_gcolor,
    int *p_bcolor, double power, complex<double> zconst, double escape_r,
    bool julia, unsigned long long &in, unsigned long long &out) {
  complex<double> point(x, y);
  complex<double> z(x, y);
  complex<double> derivative(1, 0);
  unsigned int iter_ix = 0;

  while (abs(z) < (escape_r * escape_r) && iter_ix <= iters_max) {
    z = (pow(z, power) - (0.7 / 5)) / z;
    iter_ix++;
  }

  if (iter_ix < iters_max)
    ++out;
  else
    ++in;

  if (iter_ix < iters_max) {
    get_iteration_color(iter_ix, iters_max, z, derivative, p_rcolor, p_gcolor,
                        p_bcolor);
  } else  // set interior set color
  {
    if (p_rcolor != 0) *p_rcolor = 0;
    if (p_gcolor != 0) *p_gcolor = 0;
    if (p_bcolor != 0) *p_bcolor = 0;
  }
}

complex<double> Fz6(complex<double> z) {
  return pow(z, 6) + pow(z, 3) - complex<double>(1, 0);
}

complex<double> dFz6(complex<double> z) {
  return complex<double>(6, 0) * pow(z, 5) + complex<double>(3, 0) * pow(z, 2);
}

vector<complex<double>> Fz6_roots{
    complex<double>(0.586992498352664, 1.016700830808605),
    complex<double>(-1.17398499670533, 0),
    complex<double>(0.586992498352664, -1.016700830808605),
    complex<double>(-0.4258998211039621, -0.737680128975117),
    complex<double>(0.851799642079243, 0),
    complex<double>(-0.4258998211039621, 0.737680128975117)};

void nova_z6_iterations_to_escape(double x, double y, unsigned int iters_max,
                                  int *p_rcolor, int *p_gcolor, int *p_bcolor,
                                  double power, complex<double> zconst,
                                  double escape_r, bool julia,
                                  unsigned long long &in,
                                  unsigned long long &out) {
  complex<double> point(x, y);
  complex<double> z(x, y);
  complex<double> zprev(x, y);
  complex<double> derivative(1, 0);
  unsigned int iter_ix = 0;
  double tolerance = 0.000001;

  // Mandelbrot nova
  //  zconst = z;
  //  z = Fz6_roots[0];

  while (iter_ix <= iters_max) {
    zprev = z;
    z = z - Fz6(z) / dFz6(z) + zconst;

    complex<double> diff = z - zprev;
    if ((abs(diff.real()) < tolerance) && (abs(diff.imag()) < tolerance)) {
      break;
    }
    iter_ix++;
  }

  if (iter_ix < iters_max)
    ++out;
  else
    ++in;

  if (iter_ix < iters_max) {
    get_iteration_color(iter_ix, iters_max, z, derivative, p_rcolor, p_gcolor,
                        p_bcolor);
  } else  // set interior set color
  {
    if (p_rcolor != 0) *p_rcolor = 0;
    if (p_gcolor != 0) *p_gcolor = 0;
    if (p_bcolor != 0) *p_bcolor = 0;
  }
}

void newton_z6_iterations_to_escape(double x, double y, unsigned int iters_max,
                                    int *p_rcolor, int *p_gcolor, int *p_bcolor,
                                    double power, complex<double> zconst,
                                    double escape_r, bool julia,
                                    unsigned long long &in,
                                    unsigned long long &out) {
  complex<double> point(x, y);
  complex<double> z(x, y);
  complex<double> derivative(1, 0);
  unsigned int iter_ix = 0;
  double tolerance = 0.000001;
  unsigned int which_root = 0;

  while (iter_ix <= iters_max) {
    z = z - Fz6(z) / dFz6(z);
    bool root_found = false;

    for (unsigned int i = 0; i < Fz6_roots.size(); ++i) {
      complex<double> diff = z - Fz6_roots[i];

      if ((abs(diff.real()) < tolerance) && (abs(diff.imag()) < tolerance)) {
        root_found = true;
        which_root = i;
        break;
      }
    }
    if (root_found == true) break;
    iter_ix++;
  }

  if (iter_ix < iters_max)
    ++out;
  else
    ++in;

  if (iter_ix < iters_max) {
    // found which_root
    // color the root
    //  *p_rcolor = 64+32*which_root;
    //  *p_gcolor = 255 - 32*which_root;
    //  *p_bcolor = 128 + 16*which_root;
    int color_ix = 0;
    if (R.palette == tinycolormap::ColormapType::UF16)
      color_ix = 2 + 2 * which_root;
    else
      color_ix = 1 + (iters_max / 7) * which_root;

    get_iteration_color(color_ix, iters_max, z, derivative, p_rcolor, p_gcolor,
                        p_bcolor);

    // color any root
    // get_iteration_color(iter_ix, iters_max, z, p_rcolor, p_gcolor, p_bcolor);

  } else  // set interior set color
  {
    if (p_rcolor != 0) *p_rcolor = 0;
    if (p_gcolor != 0) *p_gcolor = 0;
    if (p_bcolor != 0) *p_bcolor = 0;
  }
}

void generate_buddhabrot_trail(const complex<double> &c, unsigned int iters_max,
                               vector<complex<double>> &trail, double power,
                               complex<double> zconst, double escape_r,
                               bool julia, bool anti, unsigned long long &in,
                               unsigned long long &out) {
  unsigned int iter_ix = 0;
  complex<double> z(0, 0);
  // unsigned int max_iters_in_cycle = iters_max; //for long_orbit

  // bool long_orbit = true;

  // Modified Julia to make buddhabrot do something
  if (julia) {
    z = c;
    zconst = zconst + c;
  }

  bool cycles = true;
  if (cycles) {
    struct complex_double_hash {
      std::size_t operator()(const complex<double> &c) const {
        return std::hash<double>()(c.real()) ^ std::hash<double>()(c.imag());
      }
    };

    unordered_map<complex<double>, long, complex_double_hash> point_trail;
    // unordered_map<long, std::complex<double>> cycle_present;

    trail.clear();
    trail.reserve(iters_max + 1);

    while (iter_ix < iters_max && abs(z) < (escape_r * escape_r)) {
      if (julia)
        z = pow(z, power) + zconst;  // With Julia you dont add Point usually
      else
        z = pow(z, power) + c;
      // z = z*z + c;

      auto search = point_trail.find(z);
      if (search != point_trail.end()) {
        // max_iters_in_cycle = iter_ix;
        iter_ix = iters_max;
        break;
      }
      point_trail[z] = iter_ix;

      ++iter_ix;
      trail.push_back(z);
    }

  } else {
    trail.clear();
    trail.reserve(iters_max + 1);

    while (iter_ix < iters_max && abs(z) < 2.0) {
      if (julia)
        z = pow(z, power) + zconst;  // With Julia you dont add Point usually
      else
        z = pow(z, power) + c;
      // z = z*z + c;
      ++iter_ix;
      trail.push_back(z);
    }
  }

  //
  if (iter_ix == iters_max) {
    // ANTI
    ++in;
    if (!anti) trail.clear();  // reject in set points for regular buddhabrot

    // if ((cycles) && (max_iters_in_cycle < ((31/32) * iters_max)))
    // trail.clear(); if ((cycles) && (max_iters_in_cycle == iters_max))
    // trail.clear();

    // if (long_orbit) trail.clear();
  } else {
    // REGULAR
    ++out;

    // if (long_orbit) {
    //   if (iter_ix < (15/16)*iters_max) {trail.clear(); return;}
    //   else return;
    // }

    if (anti) trail.clear();  // reject escaped points for anti-buddhabrot
  }

  // return trail
}

// fun-illy enough we dont need the complex C++ thread sync primitives
// this is to prevent unnecessary calculation when we request 2 zooms in a row
// quickly
#define MAX_THREADS 32
bool thread_asked_to_reset[MAX_THREADS];
unsigned int thread_iteration[MAX_THREADS];
// How long each thread took for its last slice of an escape-time frame
std::atomic<double> thread_frame_ms[MAX_THREADS];
bool update_and_draw;  // stop using cpu for a bit
bool save_and_exit;
bool hide = false;

// need buddhabrot threads not to mess up model
std::mutex thread_result_report_mutex;

// Overall Model that gets drawn each cycle
// Logged once: it would otherwise repeat every frame
void logTextureFailure(sf::Vector2u size) {
  static std::atomic<bool> logged{false};
  if (logged.exchange(true)) return;
  cout << "Can't show the image: OpenGL can't make a " << size.x << "x" << size.y
       << " texture (limit " << sf::Texture::getMaximumSize()
       << "). Is the graphics driver installed?" << endl;
}

class FractalModel : public sf::Drawable, public sf::Transformable {
 public:
  FractalModel(unsigned int _view_width, unsigned int _view_height)
      : view_width{_view_width}, view_height{_view_height} {
    current_fractal = 0;
    hitsums = 0;
    maxred = 0;
    maxgreen = 0;
    maxblue = 0;
    cuda_detected = false;
    memset(&stats, 0, sizeof(stats));
    R.displayed_zoom = 1.0;
    R.requested_zoom = 1.0;
    R.xstart = FRAC[current_fractal].xMinMax[0];
    R.ystart = FRAC[current_fractal].yMinMax[0];
    R.current_height = R.original_height;
    R.current_width = R.original_width;
    R.reflect_palette = false;
    R.light_pos_r = 1;
    R.light_pos_i = 0;
    R.light_angle = 45;
    R.light_height = 1.5;
    R.xdelta =
        (FRAC[current_fractal].xMinMax[1] - FRAC[current_fractal].xMinMax[0]) /
        R.original_width;
    R.ydelta =
        (FRAC[current_fractal].yMinMax[1] - FRAC[current_fractal].yMinMax[0]) /
        R.original_height;

    deep::set_center(R.xstart + R.original_width / 2.0 * R.xdelta,
                     R.ystart + R.original_height / 2.0 * R.ydelta);

    original_view_width = view_width;
    original_view_height = view_height;
    image = sf::Image{ sf::Vector2u(view_width, view_height), sf::Color(0, 0, 0) };

    if (FRAC[current_fractal].probabalistic != true)
      panFractal(view_width / 2.0, view_height / 2.0);

    createBuddhabrot();

    color.resize(IMAGE_WIDTH);
    for (auto &v : color) v.resize(IMAGE_HEIGHT);

    stats[current_fractal].next_second_start = chrono::steady_clock::now();

  }

  ~FractalModel() {}

  // switching fractals
  void reset_fractal_params() {
    FRAC[current_fractal].current_max_iters =
        FRAC[current_fractal].default_max_iters;
    FRAC[current_fractal].current_power = FRAC[current_fractal].default_power;
    FRAC[current_fractal].current_zconst = FRAC[current_fractal].default_zconst;
    FRAC[current_fractal].current_escape_r =
        FRAC[current_fractal].default_escape_r;
  }

  // could just be tuning fractal
  void reset_fractal_and_reference_frame() {
    R.displayed_zoom = 1.0;
    R.requested_zoom = 1.0;
    R.xstart = FRAC[current_fractal].xMinMax[0];
    R.ystart = FRAC[current_fractal].yMinMax[0];
    R.xdelta =
        (FRAC[current_fractal].xMinMax[1] - FRAC[current_fractal].xMinMax[0]) /
        R.original_width;
    R.ydelta =
        (FRAC[current_fractal].yMinMax[1] - FRAC[current_fractal].yMinMax[0]) /
        R.original_height;
    deep::set_center(R.xstart + R.original_width / 2.0 * R.xdelta,
                     R.ystart + R.original_height / 2.0 * R.ydelta);
    R.current_height = R.original_height;
    R.current_width = R.original_width;
    R.show_selection = false;  // mouse click on menu is not a selection
    R.light_pos_r = 1;
    R.light_pos_i = 0;
    R.light_angle = 45;
    R.light_height = 1.5;
    hitsums = 0;
    maxred = 0;
    maxgreen = 0;
    maxblue = 0;

    memset(&stats, 0, sizeof(stats));
    if (FRAC[current_fractal].probabalistic != true) {
      zoomFractal(1.0);
    }

    for (unsigned int tix = 0; tix < this->num_threads; ++tix) {
      thread_asked_to_reset[tix] = true;
      thread_iteration[tix] = 0;
      image_wraps[tix] = 0;
      current_x[tix] = FRAC[current_fractal].xMinMax[0] + deltax * tix;
      current_y[tix] = FRAC[current_fractal].yMinMax[0] + deltay * tix;
    }

    // TODO zero the per thread hits as well
    std::lock_guard<std::mutex> guard(
        thread_result_report_mutex);  // keep out other threads
    redTrailHits.resize(0);
    greenTrailHits.resize(0);
    blueTrailHits.resize(0);
    createBuddhabrot();
  }

  // thread pool is currently started outside the model
  void fractal_thread(int tix, std::future<void> terminate, bool *p_reset,
                      unsigned int *p_iteration, bool * p_update_and_draw) {
    // cout << "fractal thread " << tix << " running with oversampling: " << 4.0
    // << endl;

    deltax = 1.0 / (4.0 * IMAGE_WIDTH);
    deltay = 1.0 / (4.0 * IMAGE_HEIGHT);
    current_x[tix] = FRAC[current_fractal].xMinMax[0] + deltax * tix;
    current_y[tix] = FRAC[current_fractal].yMinMax[0] + deltay * tix;

    vector<vector<unsigned long long>> redHits;
    vector<vector<unsigned long long>> greenHits;
    vector<vector<unsigned long long>> blueHits;

    bool reset_detected = false;

    redHits.resize(IMAGE_WIDTH);
    for (auto &v : redHits) v.resize(IMAGE_HEIGHT);
    greenHits.resize(IMAGE_WIDTH);
    for (auto &v : greenHits) v.resize(IMAGE_HEIGHT);
    blueHits.resize(IMAGE_WIDTH);
    for (auto &v : blueHits) v.resize(IMAGE_HEIGHT);

    while (1) {
      // see if we should terminate - since we're exiting anyway dont bother to
      // pass it into the threads
      if (terminate.wait_for(std::chrono::nanoseconds(0)) !=
          std::future_status::timeout) {
        // std::cout << "Terminate thread requested: " << tix << std::endl;
        break;
      }

      // Don't update if we want to draw just one
      if ((save_and_exit == true) && (p_iteration[tix] == 2)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }

      // Don't update if we want to pause cpu usage
      if (*p_update_and_draw == false) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        //std::cout << "sleep thread requested: " << tix << std::endl;
        continue;
      }

      // std::this_thread::sleep_for(std::chrono::milliseconds(10));

      // do work if we didnt terminate

      // Non Probabalistic fractals
      if (FRAC[current_fractal].probabalistic != true) {
        auto slice_start = chrono::steady_clock::now();
        reset_detected = getImagePixels(R.xstart, R.ystart, R.xdelta, R.ydelta,
                                        tix, p_reset, p_update_and_draw);
        if (reset_detected == true) {
          reset_detected = false;
          // Nothing to clear: the next pass overwrites every pixel. (This
          // used to clear() the shared color array under the other threads.)
        } else {
          thread_frame_ms[tix] = chrono::duration<double, milli>(
              chrono::steady_clock::now() - slice_start).count();
        }

        p_iteration[tix]++;
        // cout << "tix iteration: " << p_iteration[tix] << endl;

        continue;
      }

      // Probabalistic fractals

      if ((FRAC[current_fractal].cuda_mode == true) &&
          (cuda_detected == true) && has_gpu_kernel(FRAC[current_fractal])) {
        // It we are in cuda mode only allow one thread to do something
        if (tix != 0) continue;
      }

      // get trail hits - has to be much longer than sleep time above to be
      // efficient auto start = chrono::high_resolution_clock::now();

      if ((FRAC[current_fractal].cuda_mode == true) &&
          (cuda_detected == true) && has_gpu_kernel(FRAC[current_fractal])) {
        SampleStats cudastats{0, 0, 0, 0};
        // device kernel doesnt have context of model object or this c file
        if (cuda_generate_buddhabrot_hits(IMAGE_WIDTH, IMAGE_HEIGHT,
                                          FRAC[current_fractal], cudastats, redHits,
                                          greenHits, blueHits) != 0) {
          // Already logged; the other threads pick up on the CPU path
          cout << "CUDA failed, using CPU threads for the rest of this session" << endl;
          cuda_detected = false;
          for (auto *hits : {&redHits, &greenHits, &blueHits}) {
            hits->assign(IMAGE_WIDTH, vector<unsigned long long>(IMAGE_HEIGHT, 0));
          }
          continue;
        }
        stats[current_fractal].total += cudastats.total;
        stats[current_fractal].rejected += cudastats.rejected;
        stats[current_fractal].in_set += cudastats.in_set;
        stats[current_fractal].escaped_set += cudastats.escaped_set;

      } else {
        // threaded version of generate hits (we take advantage of being inside
        // model object)
        reset_detected = generateMoreTrailHits(redHits, greenHits, blueHits,
                                               &p_reset[tix], tix);
      }

      if (reset_detected == true) {
        reset_detected = false;
        // Drop the hits of the old view. Zero them: clear() kept the old
        // counts in memory and the next samples were added on top of them.
        for (auto *hits : {&redHits, &greenHits, &blueHits})
          for (auto &v : *hits) std::fill(v.begin(), v.end(), 0ULL);
        continue;  // dont merge fractal per thread trails
      }

      // auto end = chrono::high_resolution_clock::now();
      // cout << "sample time " << tix << " " <<
      // chrono::duration_cast<chrono::milliseconds>(end - start).count() << "
      // ms" << endl;

      // merge trail hits into the instance of the class (but dont make image)
      mergeHits(redHits, greenHits, blueHits);  // mutex inside
    }

    // cout << "fractal thread exiting: " << tix << endl;
  };  // buddhabrot_thread

  // for each color
  // generate a random sampling of points inside the -2 2 -2, 2 region
  // for each sample
  // get the vector trail using the per color max_iters
  // increment the point in the image size 2Darray every time it shows up in a
  // vector trail color each pixel according to the amount of times the point
  // has shown up in all the trails
  void createBuddhabrot() {
    // Initializing the 2-D vector
    redTrailHits.resize(IMAGE_WIDTH);
    for (auto &v : redTrailHits) v.resize(IMAGE_HEIGHT);
    greenTrailHits.resize(IMAGE_WIDTH);
    for (auto &v : greenTrailHits) v.resize(IMAGE_HEIGHT);
    blueTrailHits.resize(IMAGE_WIDTH);
    for (auto &v : blueTrailHits) v.resize(IMAGE_HEIGHT);

  }  // createBuddhabrot

  // Uses the GPU only if cuda_init finds a working one (it logs why not)
  void cudaPresent() { cuda_detected = cuda_init(); }

  void saveBuddhabrotTrailToColor(
      vector<complex<double>> &trail,
      vector<vector<long long unsigned int>> &colorTrailHits) {
    for (complex<double> &c : trail) {
      // if point is plottable, scale it to be on a pixel and increment the
      // value for the pixel

      double minx = FRAC[current_fractal].xMinMax[0];
      double maxx = FRAC[current_fractal].xMinMax[1];
      double miny = FRAC[current_fractal].yMinMax[0];
      double maxy = FRAC[current_fractal].yMinMax[1];
      if ((c.real() <= maxx) && (c.real() >= minx) && (c.imag() <= maxy) &&
          (c.imag() >= miny)) {
        // depending on the cast here you might get a faint gridline in your
        // image so be careful
        int x = (int)(((c.real() - minx) * R.original_width) / (maxx - minx));
        int y = (int)(((c.imag() - miny) * R.original_height) / (maxy - miny));

        colorTrailHits[x][y]++;
      }
    }
  }

  bool skipInSet(complex<double> sample) {
    if ((abs(sample - complex<double>(-1, 0)) < 0.25) ||
        (abs(1.0 - sqrt(1.0 - 4.0 * sample))) < 1.0)
      return true;
    return false;
  }

  bool generateMoreTrailHits(vector<vector<unsigned long long>> &redHits,
                             vector<vector<unsigned long long>> &greenHits,
                             vector<vector<unsigned long long>> &blueHits,
                             bool *p_reset, int tix) {
    bool reset_detected = false;

    if (image_wraps[tix] > 8) {
      // cout << "thread " << tix << " paused" << endl;
      return reset_detected;
    }

    // looks like we need to do this even if not random
    // if (R.random_sample) {
    //  Randomly sampled pixels
    std::random_device rd;
    static std::mt19937_64 re(rd());
    // uniform_real_distribution<double> xDistribution(
    //     FRAC[current_fractal].xMinMax[0], FRAC[current_fractal].xMinMax[1]);
    // uniform_real_distribution<double> yDistribution(
    //     FRAC[current_fractal].yMinMax[0], FRAC[current_fractal].yMinMax[1]);
    uniform_real_distribution<double> xDistribution(-2, 2);
    uniform_real_distribution<double> yDistribution(-2, 2);
    re.seed(chrono::high_resolution_clock::now().time_since_epoch().count());
    //}

    unsigned long long max_samples =
        100000;  // large enought to overcome thread sleep time

    for (unsigned long long s_ix = 0; s_ix < max_samples; ++s_ix) {
      // see if we should reset
      if (*p_reset == true) {
        *p_reset = false;
        // std::cout << "Reset hits thread requested: " << std::endl;
        reset_detected = true;
        break;
      }

      complex<double> sample;
      if (R.random_sample || FRAC[current_fractal].anti) {  // anti needs random samples
        // Randomly sampled pixels
        sample = {xDistribution(re), yDistribution(re)};
      } else {
        // Linearly sampled pixels
        sample = {current_x[tix], current_y[tix]};
        double xmin, xmax, ymin, ymax;

        if (FRAC[current_fractal].name == string("Anti_Buddhabrot_Small")) {
          // to draw all orbits
          xmin = -2.2;
          xmax = 1.0;
          ymin = -1.2;
          ymax = 1.2;
        } else {
          xmin = FRAC[current_fractal].xMinMax[0];
          xmax = FRAC[current_fractal].xMinMax[1];
          ymin = FRAC[current_fractal].yMinMax[0];
          ymax = FRAC[current_fractal].yMinMax[1];
        }

        double x = current_x[tix] + num_threads * deltax;
        if (x > xmax) {
          current_x[tix] = xmin + tix * deltax;
          current_y[tix] = current_y[tix] + deltay;
          if (current_y[tix] > ymax) {
            current_y[tix] = ymin;
            image_wraps[tix]++;
            if (image_wraps[tix] > 8) break;
          }
        } else
          current_x[tix] = x;
      }

      stats[current_fractal].total++;  // not atomic....

      if ((FRAC[current_fractal].current_power == 2) &&
          (FRAC[current_fractal].anti == false) &&
          (true == skipInSet(sample))) {
        stats[current_fractal].rejected++;  // not atomic....
        continue;
      }

      vector<complex<double>> trail;

      unsigned int red_max_iters = FRAC[current_fractal].current_max_iters[0];
      unsigned int green_max_iters = FRAC[current_fractal].current_max_iters[1];
      unsigned int blue_max_iters = FRAC[current_fractal].current_max_iters[2];

      generate_buddhabrot_trail(
          sample, red_max_iters, trail, FRAC[current_fractal].current_power,
          FRAC[current_fractal].current_zconst,
          FRAC[current_fractal].current_escape_r, FRAC[current_fractal].julia,
          FRAC[current_fractal].anti, stats[current_fractal].in_set,
          stats[current_fractal].escaped_set);
      saveBuddhabrotTrailToColor(trail, redHits);
      if (0 != trail.size()) {
        sample = complex<double>(sample.real(), -sample.imag());
        generate_buddhabrot_trail(
            sample, red_max_iters, trail, FRAC[current_fractal].current_power,
            FRAC[current_fractal].current_zconst,
            FRAC[current_fractal].current_escape_r, FRAC[current_fractal].julia,
            FRAC[current_fractal].anti, stats[current_fractal].in_set,
            stats[current_fractal].escaped_set);
        saveBuddhabrotTrailToColor(trail, redHits);
      }

      generate_buddhabrot_trail(
          sample, green_max_iters, trail, FRAC[current_fractal].current_power,
          FRAC[current_fractal].current_zconst,
          FRAC[current_fractal].current_escape_r, FRAC[current_fractal].julia,
          FRAC[current_fractal].anti, stats[current_fractal].in_set,
          stats[current_fractal].escaped_set);
      saveBuddhabrotTrailToColor(trail, greenHits);
      if (0 != trail.size()) {
        sample = complex<double>(sample.real(), -sample.imag());
        generate_buddhabrot_trail(
            sample, green_max_iters, trail, FRAC[current_fractal].current_power,
            FRAC[current_fractal].current_zconst,
            FRAC[current_fractal].current_escape_r, FRAC[current_fractal].julia,
            FRAC[current_fractal].anti, stats[current_fractal].in_set,
            stats[current_fractal].escaped_set);
        saveBuddhabrotTrailToColor(trail, greenHits);
      }

      generate_buddhabrot_trail(
          sample, blue_max_iters, trail, FRAC[current_fractal].current_power,
          FRAC[current_fractal].current_zconst,
          FRAC[current_fractal].current_escape_r, FRAC[current_fractal].julia,
          FRAC[current_fractal].anti, stats[current_fractal].in_set,
          stats[current_fractal].escaped_set);
      saveBuddhabrotTrailToColor(trail, blueHits);
      if (0 != trail.size()) {
        sample = complex<double>(sample.real(), -sample.imag());
        generate_buddhabrot_trail(
            sample, blue_max_iters, trail, FRAC[current_fractal].current_power,
            FRAC[current_fractal].current_zconst,
            FRAC[current_fractal].current_escape_r, FRAC[current_fractal].julia,
            FRAC[current_fractal].anti, stats[current_fractal].in_set,
            stats[current_fractal].escaped_set);
        saveBuddhabrotTrailToColor(trail, blueHits);
      }
    }
    return reset_detected;
  }

  // each thread does this under mutex
  void mergeHits(vector<vector<unsigned long long>> &redHits,
                 vector<vector<unsigned long long>> &greenHits,
                 vector<vector<unsigned long long>> &blueHits) {
    // auto start = chrono::high_resolution_clock::now();
    std::lock_guard<std::mutex> guard(
        thread_result_report_mutex);  // keep out other threads
    // auto end = chrono::high_resolution_clock::now();
    // cout << "mutex lock time " <<
    // chrono::duration_cast<chrono::milliseconds>(end - start).count() << " ms"
    // << endl; //65 ms
    unsigned long long total_merged = 0;

    for (unsigned int i = 0; i < R.original_width; i++) {
      for (unsigned int j = 0; j < R.original_height; j++) {
        total_merged += redHits[i][j];
        redTrailHits[i][j] += redHits[i][j];
        redHits[i][j] = 0;

        total_merged += greenHits[i][j];
        greenTrailHits[i][j] += greenHits[i][j];
        greenHits[i][j] = 0;

        total_merged += blueHits[i][j];
        blueTrailHits[i][j] += blueHits[i][j];
        blueHits[i][j] = 0;
      }
    }
    // auto mend = chrono::high_resolution_clock::now();
    // cout << "merge time " << chrono::duration_cast<chrono::milliseconds>(mend
    // - end).count()<< " ms for: " << total_merged << endl; //65 ms
  }

  // we wont take the mutex here since it doesnt matter
  void rebuildImageFromHits() {
    hitsums = 0;
    // for every pixel
    for (unsigned int i = 0; i < R.original_width; i++) {
      for (unsigned int j = 0; j < R.original_height; j++) {
        sf::Color pcolor{0, 0, 0};

        if (redTrailHits[i][j] > maxred) maxred = redTrailHits[i][j];

        if (greenTrailHits[i][j] > maxgreen) maxgreen = greenTrailHits[i][j];

        if (blueTrailHits[i][j] > maxblue) maxblue = blueTrailHits[i][j];

        // sqrt normalized coloring - more detail
        double rratio =
            static_cast<double>(255) / sqrt(static_cast<double>(maxred));
        int rcolor = (int)(sqrt(redTrailHits[i][j]) * rratio);
        pcolor = pcolor + sf::Color(rcolor, 0, 0);
        hitsums += redTrailHits[i][j];

        double gratio =
            static_cast<double>(255) / sqrt(static_cast<double>(maxgreen));
        int gcolor = (int)(sqrt(greenTrailHits[i][j]) * gratio);
        pcolor = pcolor + sf::Color(0, gcolor, 0);
        hitsums += greenTrailHits[i][j];

        double bratio =
            static_cast<double>(255) / sqrt(static_cast<double>(maxblue));
        int bcolor = (int)(sqrt(blueTrailHits[i][j]) * bratio);
        pcolor = pcolor + sf::Color(0, 0, bcolor);
        hitsums += blueTrailHits[i][j];

        // ratio coloring
        // double rratio = static_cast<double>(255) / maxred;
        // int rcolor = redTrailHits[i][j]*rratio;
        // pcolor = pcolor + sf::Color(rcolor,0,0);
        // hitsums += redTrailHits[i][j];

        // double gratio = static_cast<double>(255) / maxgreen;
        // int gcolor = greenTrailHits[i][j]*gratio;
        // pcolor = pcolor + sf::Color(0,gcolor,0);
        // hitsums += greenTrailHits[i][j];

        // double bratio = static_cast<double>(255) / maxblue;
        // int bcolor = blueTrailHits[i][j]*bratio;
        // pcolor = pcolor + sf::Color(0,0,bcolor);
        // hitsums += blueTrailHits[i][j];

        image.setPixel(sf::Vector2u(i, j), pcolor);
      }
    }

    if (!texture.loadFromImage(image)) logTextureFailure(image.getSize());
    sprite.emplace(texture);

    // sprite.setOrigin(800,600);
    // sprite.rotate(90.f);
  }

  bool getImagePixels(double xstart, double ystart, double xdelta,
                      double ydelta, unsigned int tix, bool *p_reset,
                      bool *p_update_and_draw) {
    bool reset_detected = false;

    // Subdivide x range by tix and num_threads
    unsigned int xrange = (unsigned int)(R.original_width / num_threads);
    unsigned int xs = tix * xrange;
    unsigned int xe = (tix + 1) * xrange;
    if (tix == num_threads - 1) xe = (unsigned int)R.original_width;

    // The USE_IMAGE coloring reads this snapshot; it stays alive (and
    // unchanged) for the whole slice even if n loads another image meanwhile
    std::shared_ptr<const EscapeImage> escape_image = snapshot_escape_image();
    t_escape_image = escape_image.get();
    struct ClearSnapshot {
      ~ClearSnapshot() { t_escape_image = nullptr; }
    } clear_snapshot;

    const SupportedFractal &f = FRAC[current_fractal];
    const unsigned int iters = effective_iters(f);  // Mandelbrot/Julia
    const bool deep_mode = deepModeFor(f, xdelta);
    bool use_gpu = f.cuda_mode && cuda_detected && has_escape_kernel(f);
    for (int attempt = 0; attempt < 2 && (use_gpu || deep_mode); ++attempt) {
      // The GPU (and/or deep zoom) computes the orbits of this thread's
      // columns; the coloring below is the CPU's own, so the look is the same
      EscapeParams p{xstart, ystart, xdelta, ydelta,
                     f.current_power, f.current_zconst.real(), f.current_zconst.imag(),
                     f.current_escape_r, R.light_pos_r, R.light_pos_i,
                     iters, f.julia ? 1 : 0,
                     R.color_algo == ColoringAlgo::SHADOW_MAP ? 1 : 0};
      unsigned int h = (unsigned int)R.original_height;
      thread_local std::vector<EscapeResult> results;
      int rc = deep_mode ? deep::render(p, (unsigned int)R.original_width, h, xs, xe, results,
                                        &p_reset[tix], use_gpu)
                         : cuda_escape_time(p, xs, xe, h, results, &p_reset[tix]);
      if (rc == CUDA_ESCAPE_RESET) {
        p_reset[tix] = false;
        return true;
      }
      if (rc == 0) {
        // Count locally: all threads updating the shared counters per pixel
        // fight over one cache line, which made this loop 20x slower
        unsigned long long in_set = 0, escaped = 0;
        for (unsigned int i = xs; i < xe; i++) {
          for (unsigned int j = 0; j < h; j++) {
            const EscapeResult &e = results[(size_t)(i - xs) * h + j];
            int rcolor = 0, gcolor = 0, bcolor = 0;
            color_escape_pixel(complex<double>(xstart + i * xdelta, ystart + j * ydelta),
                               e.iter, iters, complex<double>(e.z_r, e.z_i),
                               complex<double>(e.d_r, e.d_i), e.dist_i, e.dist_r, &rcolor,
                               &gcolor, &bcolor, in_set, escaped);
            color[i][j] = sf::Color(rcolor, gcolor, bcolor);
          }
        }
        stats[current_fractal].in_set += in_set;
        stats[current_fractal].escaped_set += escaped;
        stats[current_fractal].total += (unsigned long long)(xe - xs) * h;
        gpu_rendered = use_gpu;
        hitsums = (unsigned long long)(R.original_width * R.original_height);
        return false;
      }
      // Already logged; this thread and the others carry on with the CPU
      // (deep zoom retries this slice on the CPU)
      cout << "CUDA failed, using CPU threads for the rest of this session" << endl;
      cuda_detected = false;
      use_gpu = false;
    }
    gpu_rendered = false;

    for (unsigned int i = xs; i < xe; i++) {
      for (unsigned int j = 0; j < R.original_height; j++) {
        // see if we should reset
        if (p_reset[tix] == true) {
          p_reset[tix] = false;
          // std::cout << "Reset thread requested: " << tix << std::endl;
          reset_detected = true;
          break;
        }

        double xi = xstart + i * xdelta;
        double yj = ystart + j * ydelta;
        stats[current_fractal].total++;

        int rcolor = 0;
        int gcolor = 0;
        int bcolor = 0;

        if (FRAC[current_fractal].name == string("Spiral_Septagon"))
          spiral_septagon_iterations_to_escape(
              xi, yj, FRAC[current_fractal].current_max_iters[0], &rcolor,
              &gcolor, &bcolor, FRAC[current_fractal].current_power,
              FRAC[current_fractal].current_zconst,
              FRAC[current_fractal].current_escape_r,
              FRAC[current_fractal].julia, stats[current_fractal].in_set,
              stats[current_fractal].escaped_set);
        else if (FRAC[current_fractal].name == string("Nova_z6+z3-1")) {
          nova_z6_iterations_to_escape(
              xi, yj, FRAC[current_fractal].current_max_iters[0], &rcolor,
              &gcolor, &bcolor, FRAC[current_fractal].current_power,
              FRAC[current_fractal].current_zconst,
              FRAC[current_fractal].current_escape_r,
              FRAC[current_fractal].julia, stats[current_fractal].in_set,
              stats[current_fractal].escaped_set);
        } else if (FRAC[current_fractal].name == string("Newton_z6+z3-1")) {
          newton_z6_iterations_to_escape(
              xi, yj, FRAC[current_fractal].current_max_iters[0], &rcolor,
              &gcolor, &bcolor, FRAC[current_fractal].current_power,
              FRAC[current_fractal].current_zconst,
              FRAC[current_fractal].current_escape_r,
              FRAC[current_fractal].julia, stats[current_fractal].in_set,
              stats[current_fractal].escaped_set);
        } else
          mandelbrot_iterations_to_escape(
              xi, yj, iters, &rcolor,
              &gcolor, &bcolor, FRAC[current_fractal].current_power,
              FRAC[current_fractal].current_zconst,
              FRAC[current_fractal].current_escape_r,
              FRAC[current_fractal].julia, stats[current_fractal].in_set,
              stats[current_fractal].escaped_set);

        color[i][j] = sf::Color(rcolor, gcolor, bcolor);
      }

      if (reset_detected == true) break;
      if (*p_update_and_draw == true) {
        //break;
      }
    }
    hitsums = (unsigned long long)(R.original_width * R.original_height);

    return reset_detected;
  }

  void setImagePixels(double xstart, double ystart, double xdelta,
                      double ydelta) {
    // for every pixel
    for (unsigned int i = 0; i < R.original_width; i++) {
      for (unsigned int j = 0; j < R.original_height; j++) {
        image.setPixel(sf::Vector2u(i, j), color[i][j]);
      }
    }

    if (!texture.loadFromImage(image)) logTextureFailure(image.getSize());
    sprite.emplace(texture);
  }

  void calculateZoomWindow(double newzoom) {
    double xratio = newzoom;
    double yratio = newzoom;

    double minx = FRAC[current_fractal].xMinMax[0];
    double maxx = FRAC[current_fractal].xMinMax[1];
    double miny = FRAC[current_fractal].yMinMax[0];
    double maxy = FRAC[current_fractal].yMinMax[1];

    // mandelbrot_coordinates
    double xdelta = (maxx - minx) * xratio / R.original_width;
    double ydelta = (maxy - miny) * yratio / R.original_height;
    R.xdelta = xdelta;
    R.ydelta = ydelta;

    // after we zoom, we want to start here
    // mandelbrot coordinates
    double xstart = R.xstart + (maxx - minx) * (R.displayed_zoom - newzoom) /
                                   2.0;  // pixels

    R.xstart = xstart;

    double ystart = R.ystart + (maxy - miny) * (R.displayed_zoom - newzoom) /
                                   2.0;  // pixels

    R.ystart = ystart;

    R.current_width = newzoom * R.original_width;
    R.current_height = newzoom * R.original_height;

    R.displayed_zoom = newzoom;
    syncViewFromDeep();  // zooming keeps the center

    cout << "zoom: " << R.displayed_zoom;
    cout << "  cdims: " << R.current_width << " " << R.current_height << " ";
    cout.precision(10);
    cout << scientific << " starts: " << R.xstart << " " << R.ystart << " ";
    cout << "x range: " << scientific << xstart << " -> "
         << xstart + (R.original_width - 1) * xdelta;
    cout << "  y range: " << ystart << " -> "
         << ystart + (R.original_height - 1) * ydelta << fixed << endl;
  }

  // Assumes the user doesnt resize the window to give it different pixels
  void calculatePanWindow(double xcenter, double ycenter) {
    double xratio = R.displayed_zoom;
    double yratio = R.displayed_zoom;

    double minx = FRAC[current_fractal].xMinMax[0];
    double maxx = FRAC[current_fractal].xMinMax[1];
    double miny = FRAC[current_fractal].yMinMax[0];
    double maxy = FRAC[current_fractal].yMinMax[1];

    // mandelbrot_coordinates
    double xdelta = (maxx - minx) * xratio / R.original_width;
    double ydelta = (maxy - miny) * yratio / R.original_height;
    R.xdelta = xdelta;
    R.ydelta = ydelta;

    // after we pan we want to start here
    // mandelbrot coordinates
    double xstart = R.xstart - ((maxx - minx) / R.original_width) *
                                   (R.original_width / 2.0 - xcenter) *
                                   R.displayed_zoom;  // pixels

    R.xstart = xstart;

    double ystart = R.ystart - ((maxy - miny) / R.original_height) *
                                   (R.original_height / 2.0 - ycenter) *
                                   R.displayed_zoom;  // pixels

    R.ystart = ystart;

    // The exact (deep zoom) center moves by the same offset; the doubles are
    // refreshed from it so they stay as close as a double can
    deep::move_center((xcenter - R.original_width / 2.0) * R.xdelta,
                      (ycenter - R.original_height / 2.0) * R.ydelta);
    syncViewFromDeep();

    cout << "pan: " << xcenter << " " << ycenter << " ";
    cout << "  cdims: " << R.current_width << " " << R.current_height;
    cout.precision(10);
    cout << scientific << " starts: " << R.xstart << " " << R.ystart << " ";
    cout << "x range: " << scientific << xstart << " -> "
         << xstart + (R.original_width - 1) * xdelta;
    cout << "  y range: " << ystart << " -> "
         << ystart + (R.original_height - 1) * ydelta << fixed << endl;
  }

  // R.xstart/ystart from the exact center (they are its double view)
  void syncViewFromDeep() {
    R.xstart = deep::center_x() - R.original_width / 2.0 * R.xdelta;
    R.ystart = deep::center_y() - R.original_height / 2.0 * R.ydelta;
  }

  // Deep zoom (perturbation, deepzoom.h) once doubles run out of digits;
  // Mandelbrot and Julia with power 2 only. Logs when it switches.
  bool deepModeFor(const SupportedFractal &f, double xdelta) {
    if (!has_escape_kernel(f) || f.current_power != 2) return false;
    deep::set_depth_hint(xdelta);
    bool want = deep::wanted(xdelta);
    if (deep_active.exchange(want) != want)
      cout << (want ? "Precision: switched to deep zoom (perturbation)"
                    : "Precision: back to double")
           << ", pixel spacing " << std::scientific << std::setprecision(3) << xdelta
           << std::defaultfloat << endl;
    return want;
  }

  void zoomFractal(double newzoom) {
    if (FRAC[current_fractal].probabalistic == true) return;
    calculateZoomWindow(newzoom);
  }

  void panFractal(double xcenter, double ycenter) {
    if (FRAC[current_fractal].probabalistic == true) return;
    calculatePanWindow(xcenter, ycenter);
  }

  void update(sf::Time elapsed) {
    // draw image from latest data
    if (FRAC[current_fractal].probabalistic == true) {
      {
        std::lock_guard<std::mutex> guard(thread_result_report_mutex);
        rebuildImageFromHits();  // SetImagePixels
      }
    } else {
      setImagePixels(R.xstart, R.ystart, R.xdelta, R.ydelta);
    }

    // Update stats in Model to track how effective fractal threads,cuda are
    auto now = chrono::steady_clock::now();
    unsigned long long samples_now = stats[current_fractal].total;
    if ((now > stats[current_fractal].next_second_start) &&
        (0 != (samples_now - stats[current_fractal].samples_last_second))) {
      stats[current_fractal].samples_per_second =
          (1000 * (samples_now - stats[current_fractal].samples_last_second)) /
          (1000 + chrono::duration_cast<chrono::milliseconds>(
                      now - stats[current_fractal].next_second_start)
                      .count());
      stats[current_fractal].next_second_start =
          chrono::steady_clock::now() + std::chrono::milliseconds(1000);

      // cout << stats[current_fractal].samples_per_second << " " <<
      // (samples_now - stats[current_fractal].samples_last_second) << endl;

      stats[current_fractal].samples_last_second = samples_now;
    }
  }

 private:
  virtual void draw(sf::RenderTarget &target, sf::RenderStates states) const {
    // apply the transform
    states.transform *= getTransform();

    // our particles don't use a texture
    states.texture = NULL;

    // draw the image (just one for now - could do multiple fractals blended)
    if (sprite.has_value()) {
        target.draw(*sprite, states);
    }
  }

 public:
  unsigned int current_fractal;
  std::atomic<bool> cuda_detected;  // cleared by a render thread if CUDA fails
  std::atomic<bool> gpu_rendered{false};  // the last escape-time slice came from the GPU
  std::atomic<bool> deep_active{false};   // rendering with deep zoom (perturbation)
  unsigned int view_width;
  unsigned int view_height;
  unsigned long long maxred = 0;
  unsigned long long maxgreen = 0;
  unsigned long long maxblue = 0;
  unsigned long long hitsums = 0;

  SampleStats stats[16];  // indexed by fractal

  unsigned int num_threads;

  // point to try next if not using random sampling
  double current_x[MAX_THREADS];
  double current_y[MAX_THREADS];
  int image_wraps[MAX_THREADS];
  double deltax;
  double deltay;

 private:
  double original_view_width;
  double original_view_height;
  sf::Image image;
  sf::Texture texture;
  std::optional<sf::Sprite> sprite;

  // merged hits from threads
  vector<vector<unsigned long long>> redTrailHits;
  vector<vector<unsigned long long>> greenTrailHits;
  vector<vector<unsigned long long>> blueTrailHits;

  // Non buddha fractals
  vector<vector<sf::Color>> color;
};  // FractalModel

// Now we try to do the control elements displayed inside the view GUI that
// control both the model and the view (not just the view)

void setGuiElementsFromModel(shared_ptr<tgui::Gui> &pgui,
                             shared_ptr<FractalModel> &p_model);

void updateGuiElements(shared_ptr<tgui::Gui> &pgui,
                       shared_ptr<FractalModel> &p_model);

// ---------------------------------------------------------------------------
// Number boxes. A value applies when you press Enter or leave the box, and only
// if the text is a complete number; anything else (empty, "-", out of range)
// changes nothing. Then the box is cleared, so its grey default text shows the
// value in effect (refreshed every frame by refreshNumberBoxes). Applying as
// you typed used every prefix: deleting "500" left 5 iterations, and typing a
// big number briefly asked for 999999999.
// ---------------------------------------------------------------------------
std::optional<double> parseNumber(const tgui::String &text) {
  std::string t = text.toStdString();
  if (t.empty()) return std::nullopt;
  try {
    size_t used = 0;
    double v = std::stod(t, &used);
    if (used != t.size() || !std::isfinite(v)) return std::nullopt;
    return v;
  } catch (...) {  // invalid_argument, out_of_range
    return std::nullopt;
  }
}

// base 0 also accepts 0x... (hex), as the interior color box did before
std::optional<unsigned int> parseCount(const tgui::String &text, int base = 10) {
  std::string t = text.toStdString();
  if (t.empty() || t[0] == '-' || t[0] == '+') return std::nullopt;
  try {
    size_t used = 0;
    unsigned long long v = std::stoull(t, &used, base);
    if (used != t.size() || v > UINT_MAX) return std::nullopt;
    return (unsigned int)v;
  } catch (...) {
    return std::nullopt;
  }
}

// 574207584 -> "574.2 M"
std::string formatCount(double v) {
  const char *units[] = {"", " k", " M", " G", " T"};
  int u = 0;
  while (v >= 1000 && u < 4) v /= 1000, ++u;
  std::ostringstream out;
  out << std::fixed << std::setprecision(u ? 1 : 0) << v << units[u];
  return out.str();
}

std::string formatPercent(double part, double whole) {
  if (whole <= 0) return "-";
  std::ostringstream out;
  out << std::fixed << std::setprecision(1) << 100.0 * part / whole << "%";
  return out.str();
}

std::string formatNumber(double v) {
  std::ostringstream out;
  out << std::setprecision(10) << v;
  return out.str();
}

const char *number_boxes[] = {"power_box",       "max_iters_box0",  "max_iters_box1",
                              "max_iters_box2",  "zconst_real_box", "zconst_imag_box",
                              "escape_r_box",    "interior_color_adjust"};

void refreshNumberBoxes(shared_ptr<tgui::Gui> &pgui, shared_ptr<FractalModel> &p_model) {
  const SupportedFractal &f = FRAC[p_model->current_fractal];
  const std::string values[] = {formatNumber(f.current_power),
                                to_string(f.current_max_iters[0]),
                                to_string(f.current_max_iters[1]),
                                to_string(f.current_max_iters[2]),
                                formatNumber(f.current_zconst.real()),
                                formatNumber(f.current_zconst.imag()),
                                formatNumber(f.current_escape_r),
                                to_string(interior_color_adjust)};
  for (size_t i = 0; i < std::size(number_boxes); ++i) {
    auto box = pgui->get<tgui::EditBox>(number_boxes[i]);
    if (box && box->getDefaultText() != values[i]) box->setDefaultText(values[i]);
  }
}

// After a fractal switch, key load or undo: drop typed text so every box shows
// the new values
void clearNumberBoxes(shared_ptr<tgui::Gui> &pgui) {
  for (const char *name : number_boxes)
    if (auto box = pgui->get<tgui::EditBox>(name)) box->setText("");
}

void signalFractalMenu(shared_ptr<FractalModel> p_model,
                       shared_ptr<tgui::Gui> pgui, const tgui::String &selected) {
  for (size_t i = 0; i < FRAC.size(); ++i) {
    if (selected == FRAC[i].name) {
      p_model->current_fractal = (unsigned int)i;
    }
  }
  updateGuiElements(pgui, p_model);
  p_model->reset_fractal_and_reference_frame();
  p_model->reset_fractal_params();
  if (FRAC[p_model->current_fractal].anti) R.random_sample = true;  // required there
  clearNumberBoxes(pgui);
  setGuiElementsFromModel(pgui, p_model);
}

void signalPower(shared_ptr<FractalModel> p_model,
                 shared_ptr<tgui::Gui> pgui, const tgui::String &value) {
  struct Clear { shared_ptr<tgui::Gui> g; ~Clear() { clearNumberBoxes(g); } } clear{pgui};
  auto v = parseNumber(value);
  if (!v || *v == FRAC[p_model->current_fractal].current_power) return;
  updateGuiElements(pgui, p_model);
  FRAC[p_model->current_fractal].current_power = *v;
  p_model->reset_fractal_and_reference_frame();
  setGuiElementsFromModel(pgui, p_model);
}

void signalMIters(shared_ptr<FractalModel> p_model,
		  shared_ptr<tgui::Gui> pgui, int iter_ix, const tgui::String &value) {
  struct Clear { shared_ptr<tgui::Gui> g; ~Clear() { clearNumberBoxes(g); } } clear{pgui};
  if (auto v = parseCount(value))
    FRAC[p_model->current_fractal].current_max_iters[iter_ix] = *v;
}

void signalZconstr(shared_ptr<FractalModel> p_model,
                   shared_ptr<tgui::Gui> pgui, const tgui::String &value) {
  struct Clear { shared_ptr<tgui::Gui> g; ~Clear() { clearNumberBoxes(g); } } clear{pgui};
  complex<double> &z = FRAC[p_model->current_fractal].current_zconst;
  auto v = parseNumber(value);
  if (!v || *v == z.real()) return;
  updateGuiElements(pgui, p_model);
  z = complex<double>(*v, z.imag());
  p_model->reset_fractal_and_reference_frame();
  setGuiElementsFromModel(pgui, p_model);
}

void signalZconsti(shared_ptr<FractalModel> p_model,
                   shared_ptr<tgui::Gui> pgui, const tgui::String &value) {
  struct Clear { shared_ptr<tgui::Gui> g; ~Clear() { clearNumberBoxes(g); } } clear{pgui};
  complex<double> &z = FRAC[p_model->current_fractal].current_zconst;
  auto v = parseNumber(value);
  if (!v || *v == z.imag()) return;
  updateGuiElements(pgui, p_model);
  z = complex<double>(z.real(), *v);
  p_model->reset_fractal_and_reference_frame();
  setGuiElementsFromModel(pgui, p_model);
}

void signal_escape_r(shared_ptr<FractalModel> p_model,
                     shared_ptr<tgui::Gui> pgui, const tgui::String &value) {
  struct Clear { shared_ptr<tgui::Gui> g; ~Clear() { clearNumberBoxes(g); } } clear{pgui};
  auto v = parseNumber(value);
  if (!v || *v <= 0) return;  // 0 would make every point escape at once
  updateGuiElements(pgui, p_model);
  FRAC[p_model->current_fractal].current_escape_r = *v;
  setGuiElementsFromModel(pgui, p_model);
}

void signalSamplingButton(shared_ptr<FractalModel> p_model, bool checked) {
  if (R.random_sample == checked) return;  // set from the model, nothing changed
  R.random_sample = checked;

  for (unsigned int tix = 0; tix < p_model->num_threads; ++tix) {
    thread_asked_to_reset[tix] = true;
    thread_iteration[tix] = 0;
    p_model->image_wraps[tix] = 0;
    p_model->current_x[tix] =
        FRAC[p_model->current_fractal].xMinMax[0] + p_model->deltax * tix;
    p_model->current_y[tix] =
        FRAC[p_model->current_fractal].yMinMax[0] + p_model->deltay * tix;
  }
}

// The lists show these in this order; the index is the list position
const ColoringAlgo outside_styles[] = {ColoringAlgo::MULTICYCLE, ColoringAlgo::SMOOTH,
                                       ColoringAlgo::USE_IMAGE, ColoringAlgo::SHADOW_MAP};
const char *outside_style_names[] = {"Bands", "Smooth", "Image", "3D light"};
const InteriorColoringAlgo inside_styles[] = {
    InteriorColoringAlgo::SOLID, InteriorColoringAlgo::MULTICYCLE, InteriorColoringAlgo::USE_IMAGE,
    InteriorColoringAlgo::TRIG,  InteriorColoringAlgo::DIST,       InteriorColoringAlgo::DIST2};
const char *inside_style_names[] = {"Solid color",    "Orbit bands", "Image",
                                    "Orbit angle",    "Orbit length", "Length waves"};

// Band size list: 16, 32, ... 1024 (16 << index); other sizes from old keys
// select the nearest entry at or above them
int cycleIndex(int cycle_size) {
  int i = 0;
  while (i < 6 && (16 << i) < cycle_size) ++i;
  return i;
}

void signalColorBox(const int selected) {
  if (selected >= 0) R.palette = static_cast<tinycolormap::ColormapType>(selected);
}

void signalColorCycleBox(const int selected) {
  if (selected >= 0) R.color_cycle_size = 16 << selected;
}

void signalCAlgoBox(const int selected) {
  if (selected < 0 || selected >= (int)std::size(outside_styles)) return;
  ColoringAlgo a = outside_styles[selected];
  if (a == ColoringAlgo::USE_IMAGE && !R.image_loaded) return;  // no image to use
  R.color_algo = a;
}

void signalButton(bool checked) { R.reflect_palette = checked; }

// Interior Color
void signalIntColorAdj(shared_ptr<FractalModel> p_model,
                       shared_ptr<tgui::Gui> pgui, const tgui::String &value) {
  struct Clear { shared_ptr<tgui::Gui> g; ~Clear() { clearNumberBoxes(g); } } clear{pgui};
  if (auto v = parseCount(value, 0)) interior_color_adjust = *v;
}

void signalIntColorBox(const int selected) {
  if (selected >= 0) RI.palette = static_cast<tinycolormap::ColormapType>(selected);
}

void signalIntColorCycleBox(const int selected) {
  if (selected >= 0) RI.color_cycle_size = 16 << selected;
}

void signalIntCAlgoBox(const int selected) {
  if (selected < 0 || selected >= (int)std::size(inside_styles)) return;
  InteriorColoringAlgo a = inside_styles[selected];
  if (a == InteriorColoringAlgo::USE_IMAGE && !R.image_loaded) return;
  RI.color_algo = a;
}

void signalIntButton(bool checked) { RI.reflect_palette = checked; }

const int max_saved = 30;
SavedFractal no_fractal{0, 1.0};
int last_loaded_key_ix = -1;
int key_count = 0;  // keys found the last time "Load Next Key" looked
std::string loaded_key_name;  // file name (no extension) of the key last loaded
int frac_ix = 0;
int displayed_frac_ix = -1;
vector<SavedFractal> savf(max_saved,
                          no_fractal);  // for saving good looking ones
SavedFractal Last(no_fractal);          // for undo

// The current fractal description: type, parameters, view, coloring, lighting
SavedFractal captureFractal(shared_ptr<FractalModel> p_model) {
  const SupportedFractal &f = FRAC[p_model->current_fractal];
  SavedFractal s = no_fractal;
  s.version = FRACTAL_VERSION;
  s.valid = 1;
  s.current_fractal = p_model->current_fractal;
  s.current_power = f.current_power;
  for (int i = 0; i < 3; ++i) s.current_max_iters[i] = f.current_max_iters[i];
  s.current_zconst = f.current_zconst;
  s.current_escape_r = f.current_escape_r;
  s.RF = R;
  s.RI = RI;
  s.center_x = deep::center_x_str();
  s.center_y = deep::center_y_str();
  return s;
}

// Switches to a saved fractal. Only the description is taken from it; the
// window size, the loaded escape image and the selection stay as they are, and
// the per-pixel view values are recalculated for the current window size.
void applyFractal(shared_ptr<FractalModel> p_model, const SavedFractal &s) {
  p_model->current_fractal = s.current_fractal;
  p_model->reset_fractal_and_reference_frame();

  SupportedFractal &f = FRAC[s.current_fractal];
  f.current_power = s.current_power;
  for (int i = 0; i < 3; ++i) f.current_max_iters[i] = s.current_max_iters[i];
  f.current_zconst = s.current_zconst;
  f.current_escape_r = s.current_escape_r;

  const ReferenceFrame &r = s.RF;
  R.theta = r.theta;
  R.xstart = r.xstart;
  R.ystart = r.ystart;
  R.displayed_zoom = r.displayed_zoom;
  R.requested_zoom = r.requested_zoom;
  // The exact center: from the key's strings, else from x_start/y_start. The
  // pan and zoom below keep it and refresh R.xstart/ystart from it.
  const SupportedFractal &fr = FRAC[s.current_fractal];
  if (s.center_x.empty() || !deep::set_center(s.center_x, s.center_y))
    deep::set_center(R.xstart + (fr.xMinMax[1] - fr.xMinMax[0]) * R.displayed_zoom / 2.0,
                     R.ystart + (fr.yMinMax[1] - fr.yMinMax[0]) * R.displayed_zoom / 2.0);
  R.color_algo = r.color_algo;
  R.color_cycle_size = r.color_cycle_size;
  R.palette = r.palette;
  R.reflect_palette = r.reflect_palette;
  R.light_pos_r = r.light_pos_r;
  R.light_pos_i = r.light_pos_i;
  R.light_angle = r.light_angle;
  R.light_height = r.light_height;
  R.random_sample = r.random_sample || f.anti;  // the anti-Buddhabrots need it
  R.auto_iterations = r.auto_iterations;
  if (R.color_algo == ColoringAlgo::USE_IMAGE && !R.image_loaded)
    R.color_algo = ColoringAlgo::MULTICYCLE;
  RI = s.RI;

  // A pan to the center recalculates the per-pixel deltas for this window;
  // then zoom about the center from the saved zoom to the requested one (the
  // movie script animates requested_zoom)
  p_model->panFractal(R.original_width / 2.0, R.original_height / 2.0);
  p_model->zoomFractal(R.requested_zoom);
}

void signalSaveFractal(shared_ptr<FractalModel> p_model,
                       shared_ptr<tgui::Gui> pgui) {
  updateGuiElements(pgui, p_model);
  savf[frac_ix] = captureFractal(p_model);
  frac_ix = (frac_ix + 1) % max_saved;
  setGuiElementsFromModel(pgui, p_model);
}

void SaveLast(shared_ptr<FractalModel> p_model) {
  Last = captureFractal(p_model);
}

// ---------------------------------------------------------------------------
// Fractal keys: JSON files in FractalsData/keys. Fields are looked up by name,
// a missing field keeps its default and unknown fields are ignored, so adding
// fields never breaks old keys. Enums are stored by name.
// ---------------------------------------------------------------------------
const int KEY_FORMAT_VERSION = 1;

/* CRC-32 (Ethernet, ZIP, etc.) polynomial in reversed bit order. */
#define POLY 0xedb88320

uint32_t crc32c(uint32_t crc, const unsigned char *buf, size_t len) {
  int k;

  crc = ~crc;
  while (len--) {
    crc ^= *buf++;
    for (k = 0; k < 8; k++) crc = crc & 1 ? (crc >> 1) ^ POLY : crc >> 1;
  }
  return ~crc;
}

json keyToJson(const SavedFractal &s) {
  const ReferenceFrame &r = s.RF;
  json j;
  j["format_version"] = KEY_FORMAT_VERSION;
  j["fractal"] = FRAC[s.current_fractal].name;
  j["max_iterations"] = json::array(
      {s.current_max_iters[0], s.current_max_iters[1], s.current_max_iters[2]});
  j["power"] = s.current_power;
  j["zconst"] = json::array({s.current_zconst.real(), s.current_zconst.imag()});
  j["escape_radius"] = s.current_escape_r;
  j["random_sample"] = r.random_sample;
  j["auto_iterations"] = r.auto_iterations;
  j["view"] = {{"center_x", s.center_x},  // exact; x_start/y_start are doubles
               {"center_y", s.center_y},
               {"x_start", r.xstart},
               {"y_start", r.ystart},
               {"zoom", r.displayed_zoom},
               {"requested_zoom", r.requested_zoom},
               {"theta", r.theta}};
  j["coloring"] = {{"algo", r.color_algo},
                   {"cycle_size", r.color_cycle_size},
                   {"palette", r.palette},
                   {"reflect_palette", r.reflect_palette}};
  j["lighting"] = {{"pos_r", r.light_pos_r},
                   {"pos_i", r.light_pos_i},
                   {"angle", r.light_angle},
                   {"height", r.light_height}};
  j["interior"] = {{"algo", s.RI.color_algo},
                   {"cycle_size", s.RI.color_cycle_size},
                   {"palette", s.RI.palette},
                   {"reflect_palette", s.RI.reflect_palette}};
  return j;
}

// j[key], or def when it's missing or has the wrong type
template <class T>
T keyField(const json &j, const char *key, T def) {
  auto it = j.find(key);
  if (it == j.end()) return def;
  try {
    return it->get<T>();
  } catch (const json::exception &) {
    cout << "key field \"" << key << "\" has the wrong type, using the default" << endl;
    return def;
  }
}

// j[group][key], or def
template <class T>
T keyField(const json &j, const char *group, const char *key, T def) {
  auto g = j.find(group);
  if (g == j.end() || !g->is_object()) return def;
  return keyField(*g, key, def);
}

bool keyFromJson(const json &j, SavedFractal &s) {
  if (!j.is_object()) {
    cout << "key is not a JSON object" << endl;
    return false;
  }
  int format = keyField(j, "format_version", 0);
  if (format > KEY_FORMAT_VERSION)
    cout << "key format_version " << format << " is newer than this app ("
         << KEY_FORMAT_VERSION << "); unknown fields are ignored" << endl;

  std::string name = keyField(j, "fractal", std::string{});
  auto found = std::find_if(FRAC.begin(), FRAC.end(),
                            [&](const SupportedFractal &f) { return f.name == name; });
  if (found == FRAC.end()) {
    cout << "key names an unknown fractal: \"" << name << "\"" << endl;
    return false;
  }
  const SupportedFractal &f = *found;

  s = no_fractal;
  s.version = FRACTAL_VERSION;
  s.valid = 1;
  s.current_fractal = (unsigned int)(found - FRAC.begin());

  std::vector<unsigned int> iters = keyField(j, "max_iterations", f.default_max_iters);
  for (int i = 0; i < 3; ++i)
    s.current_max_iters[i] = i < (int)iters.size() ? iters[i] : f.default_max_iters[i];
  s.current_power = keyField(j, "power", f.default_power);
  std::vector<double> z = keyField(j, "zconst", std::vector<double>{});
  s.current_zconst = z.size() == 2 ? std::complex<double>(z[0], z[1]) : f.default_zconst;
  s.current_escape_r = keyField(j, "escape_radius", f.default_escape_r);

  // Defaults are what the app starts with
  ReferenceFrame &r = s.RF;
  r.random_sample = keyField(j, "random_sample", true);
  r.auto_iterations = keyField(j, "auto_iterations", false);  // old keys: fixed
  r.xstart = keyField(j, "view", "x_start", f.xMinMax[0]);
  r.ystart = keyField(j, "view", "y_start", f.yMinMax[0]);
  r.displayed_zoom = keyField(j, "view", "zoom", 1.0);
  r.requested_zoom = keyField(j, "view", "requested_zoom", r.displayed_zoom);
  r.theta = keyField(j, "view", "theta", 0.0f);
  s.center_x = keyField(j, "view", "center_x", std::string{});
  s.center_y = keyField(j, "view", "center_y", std::string{});
  r.color_algo = keyField(j, "coloring", "algo", ColoringAlgo::MULTICYCLE);
  r.color_cycle_size = keyField(j, "coloring", "cycle_size", 32);
  r.palette = keyField(j, "coloring", "palette", tinycolormap::ColormapType::UF16);
  r.reflect_palette = keyField(j, "coloring", "reflect_palette", false);
  r.light_pos_r = keyField(j, "lighting", "pos_r", 1.0);
  r.light_pos_i = keyField(j, "lighting", "pos_i", 0.0);
  r.light_angle = keyField(j, "lighting", "angle", 45.0);
  r.light_height = keyField(j, "lighting", "height", 1.5);
  s.RI.color_algo = keyField(j, "interior", "algo", InteriorColoringAlgo::SOLID);
  s.RI.color_cycle_size = keyField(j, "interior", "cycle_size", 256);
  s.RI.palette = keyField(j, "interior", "palette", tinycolormap::ColormapType::UF16);
  s.RI.reflect_palette = keyField(j, "interior", "reflect_palette", false);
  return true;
}

bool writeKey(const fs::path &filename, const SavedFractal &s) {
  std::ofstream f(filename);
  f << keyToJson(s).dump(2) << "\n";
  if (!f) cout << "failed to write key " << filename.string() << endl;
  return (bool)f;
}

bool readKey(const fs::path &filename, SavedFractal &s) {
  std::ifstream f(filename);
  if (!f) {
    cout << "can't open key " << filename.string() << endl;
    return false;
  }
  json j = json::parse(f, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);
  if (j.is_discarded()) {
    cout << "key is not valid JSON: " << filename.string() << endl;
    return false;
  }
  return keyFromJson(j, s);
}

// Saves the current fractal. infname (no extension) is used by --save-and-exit;
// otherwise the key goes to FractalsData/keys/<fractal>_<crc>.json.
void signalSaveKey(shared_ptr<FractalModel> p_model, shared_ptr<tgui::Gui> pgui,
                   std::string infname = "") {
  updateGuiElements(pgui, p_model);

  savf[frac_ix] = captureFractal(p_model);
  const SavedFractal &s = savf[frac_ix];
  frac_ix = (frac_ix + 1) % max_saved;

  fs::path filename;
  if (infname != "") {
    filename = infname + ".json";
  } else {
    std::string text = keyToJson(s).dump();
    uint32_t crc = crc32c(0, reinterpret_cast<const unsigned char *>(text.data()), text.size());
    filename = fs::path(keys_location) / (FRAC[s.current_fractal].name + "_" + to_string(crc) + ".json");
  }
  if (writeKey(filename, s))
    cout << "saved: " << filename.string() << " " << FRAC[s.current_fractal].name << endl;

  setGuiElementsFromModel(pgui, p_model);
}

void signalLoadNextSaved(shared_ptr<FractalModel> p_model,
                         shared_ptr<tgui::Gui> pgui) {
  updateGuiElements(pgui, p_model);

  int ix = displayed_frac_ix;
  for (int tried = 0; tried < max_saved; ++tried) {
    ix = (ix + 1) % max_saved;
    if (savf[ix].valid != 0) {
      displayed_frac_ix = ix;
      applyFractal(p_model, savf[ix]);
      break;
    }
  }

  clearNumberBoxes(pgui);
  setGuiElementsFromModel(pgui, p_model);
}

void LoadLast(shared_ptr<FractalModel> p_model, shared_ptr<tgui::Gui> pgui) {
  updateGuiElements(pgui, p_model);
  if (Last.valid) applyFractal(p_model, Last);
  clearNumberBoxes(pgui);
  setGuiElementsFromModel(pgui, p_model);
}

// --save-and-exit: returns non-zero if the key can't be loaded
int LoadProvidedKey(shared_ptr<FractalModel> p_model,
                    shared_ptr<tgui::Gui> pgui, std::string keyname) {
  updateGuiElements(pgui, p_model);

  SavedFractal s = no_fractal;
  if (!readKey(keyname, s)) return 1;
  applyFractal(p_model, s);
  cout << "LOADED PASSED IN KEY: " << keyname << " " << FRAC[s.current_fractal].name
       << " zoom " << R.displayed_zoom << " start " << R.xstart << " " << R.ystart << endl;

  setGuiElementsFromModel(pgui, p_model);
  return 0;
}

// Loads the keys in FractalsData/keys one after another, in name order
void signalLoadNextKey(shared_ptr<FractalModel> p_model,
                       shared_ptr<tgui::Gui> pgui) {
  updateGuiElements(pgui, p_model);

  std::vector<fs::path> keys;
  std::error_code ec;
  for (auto &p : fs::directory_iterator(keys_location, ec))
    if (p.path().extension() == ".json") keys.push_back(p.path());
  std::sort(keys.begin(), keys.end());
  key_count = (int)keys.size();

  // Skip keys that fail to load
  for (size_t tried = 0; tried < keys.size(); ++tried) {
    last_loaded_key_ix = (last_loaded_key_ix + 1) % (int)keys.size();
    SavedFractal s = no_fractal;
    if (readKey(keys[last_loaded_key_ix], s)) {
      applyFractal(p_model, s);
      loaded_key_name = keys[last_loaded_key_ix].stem().string();
      cout << "loaded: " << keys[last_loaded_key_ix].string() << endl;
      break;
    }
  }

  clearNumberBoxes(pgui);
  setGuiElementsFromModel(pgui, p_model);
}

std::string escape_dir;  // data_dir/escape_images, set in main

int escape_count = 0;
int escape_ix = -1;
void signalLoadNextEscape(shared_ptr<FractalModel> p_model,
                          shared_ptr<tgui::Gui> pgui) {
  updateGuiElements(pgui, p_model);

  int ix = 0;

  escape_count = 0;
  for (auto &p : fs::directory_iterator(escape_dir)) {
    //std::cout << p.path() << '\n';
    escape_count++;
  }
  if (escape_count == 0) return;

  if (escape_ix == escape_count - 1) escape_ix = -1;

  std::string filename{""};
  for (auto &p : fs::directory_iterator(escape_dir)) {
    if ((escape_count == 1) || (ix > escape_ix)) {
      filename = p.path().string();
      escape_ix = ix;
      break;
    }
    ix++;
  }

  load_escape_image(filename);

  setGuiElementsFromModel(pgui, p_model);
}

// Hover help for a widget
void setTip(const tgui::Widget::Ptr &w, const std::string &text) {
  auto tip = tgui::Label::create(text);
  tip->setTextSize(14);
  auto r = tip->getRenderer();
  r->setBackgroundColor(tgui::Color(25, 25, 25, 240));
  r->setTextColor(tgui::Color::White);
  r->setBorders(1);
  r->setBorderColor(tgui::Color(120, 120, 120));
  r->setPadding({6, 4});
  w->setToolTip(tip);
}

// A small caption above a column of controls
void addHeading(shared_ptr<tgui::Gui> &pgui, const char *text, const char *x, const char *y) {
  auto l = tgui::Label::create(text);
  l->setTextSize(13);
  l->setPosition(x, y);
  pgui->add(l);
}

// The main GUI elements inside the view
void createGuiElements(shared_ptr<tgui::Gui> pgui,
                       shared_ptr<FractalModel> &p_model) {
  // Current and Menu Group
  auto current = tgui::Label::create();
  current->setPosition("parent.left", "parent.bottom - 300");
  current->setTextSize(14);
  pgui->add(current, "fractal_label");


  current = tgui::Label::create();
  current->setPosition("parent.left", "parent.bottom - 300 + 20");
  current->setTextSize(14);
  pgui->add(current, "secs_label");





  current = tgui::Label::create();
  current->setPosition("parent.left", "parent.bottom - 300 + 40");
  current->setTextSize(14);
  pgui->add(current, "boundary_label");

  current = tgui::Label::create();
  current->setPosition("parent.left", "parent.bottom - 300 + 60");
  current->setTextSize(14);
  pgui->add(current, "stats_label");

  auto menu = tgui::MenuBar::create();
  menu->setPosition("parent.left", "parent.bottom - 300 - 30");
  menu->setSize(200.f, 22.f);
  menu->addMenu("Fractal");
  for (auto frac : FRAC) {
    menu->addMenuItem(frac.name);
  }
  menu->addMenu("Help");
  menu->addMenuItem("Use mouse wheel to zoom (some fractals)");
  menu->addMenuItem("Use right mouse button to recenter (some fractals)");
  menu->addMenuItem("Hold and drag left mouse button to zoom to cropped area (some fractals)");
  menu->addMenuItem("Type h to hide/show fractal");
  menu->addMenuItem("Type g to hide/show gui");
  menu->addMenuItem("Type p to pause/resume fractal generation");
  menu->addMenuItem("Type c to turn cuda on/off");
  menu->addMenuItem("Type s to take a screenshot");
  menu->addMenuItem("Type z to undo last zoom/pan");
  menu->addMenuItem("Type n to load next coloring escape image");
  menu->addMenuItem("Type e to exit");

  //pgui->add(menu); //added at end so its always on top
  for (auto frac : FRAC) {
    menu->connectMenuItem("Fractal", frac.name, signalFractalMenu, p_model, pgui, frac.name);
  }

  // Params Group
  current = tgui::Label::create();
  current->setPosition("parent.left + 50", "parent.bottom - 150 - 20");
  current->setTextSize(14);
  pgui->add(current, "power_label");

  auto editBox = tgui::EditBox::create();
  editBox->setSize(100, 20);
  editBox->setTextSize(14);
  editBox->setPosition("parent.left + 50", "parent.bottom - 150");
  editBox->setDefaultText("2");
  pgui->add(editBox, "power_box");
  setTip(editBox, "Power p in z -> z^p + c (2 is the classic Mandelbrot).\nType a number and press Enter.");
  editBox->onReturnOrUnfocus(signalPower, p_model, pgui);

  current = tgui::Label::create();
  current->setPosition("parent.left + 50", "parent.bottom - 210");
  current->setTextSize(14);
  pgui->add(current, "miters_label");

  editBox = tgui::EditBox::create();
  editBox->setSize(100, 20);
  editBox->setTextSize(14);
  editBox->setPosition("parent.left + 50 + 120", "parent.bottom - 150");
  editBox->setDefaultText("");
  pgui->add(editBox, "max_iters_box2");
  setTip(editBox, "Max iterations (Buddhabrot: blue channel): how long an orbit is followed\n"
                  "before the point counts as inside the set. More shows finer detail\n"
                  "but takes longer. Mandelbrot and Julia use the top box only.");
  editBox->onReturnOrUnfocus(signalMIters, p_model, pgui,2);

  editBox = tgui::EditBox::create();
  editBox->setSize(100, 20);
  editBox->setTextSize(14);
  editBox->setPosition("parent.left + 50 + 120", "parent.bottom - 180");
  editBox->setDefaultText("");
  pgui->add(editBox, "max_iters_box1");
  setTip(editBox, "Max iterations (Buddhabrot: green channel): how long an orbit is followed\n"
                  "before the point counts as inside the set. More shows finer detail\n"
                  "but takes longer. Mandelbrot and Julia use the top box only.");
  editBox->onReturnOrUnfocus(signalMIters, p_model, pgui, 1);

  editBox = tgui::EditBox::create();
  editBox->setSize(100, 20);
  editBox->setTextSize(14);
  editBox->setPosition("parent.left + 50 + 120", "parent.bottom - 210");
  editBox->setDefaultText("");
  pgui->add(editBox, "max_iters_box0");
  setTip(editBox, "Max iterations (Buddhabrot: red channel): how long an orbit is followed\n"
                  "before the point counts as inside the set. More shows finer detail\n"
                  "but takes longer. Mandelbrot and Julia use the top box only.");
  editBox->onReturnOrUnfocus(signalMIters, p_model, pgui, 0);

  auto cbox = tgui::CheckBox::create();
  cbox->setPosition("parent.left + 50", "parent.bottom - 190");
  cbox->setSize(16, 16);
  cbox->setText("Auto");
  cbox->setTextSize(13);
  cbox->setChecked(R.auto_iterations);
  pgui->add(cbox, "AutoIterations");
  setTip(cbox, "Auto iterations (Mandelbrot and Julia): the value in the box is a base that\n"
               "grows as you zoom in (x5 at zoom 1e-4, x14 at 1e-13). Points near the\n"
               "boundary need more steps to escape; with too few they count as inside.\n"
               "Off: the box value is used as is.");
  cbox->onChange([p_model](bool checked) {
    if (R.auto_iterations == checked) return;
    R.auto_iterations = checked;
    for (unsigned int tix = 0; tix < p_model->num_threads; ++tix) thread_asked_to_reset[tix] = true;
  });

  cbox = tgui::CheckBox::create();
  cbox->setPosition("parent.left + 50 + 250", "parent.bottom - -210");
  cbox->setText("Random\nsampling");
  cbox->setSize(30, 30);
  pgui->add(cbox, "RandomSample");
  setTip(cbox, "Buddhabrot: pick sample points at random instead of on a grid.\n"
               "Always on for the anti-Buddhabrots, which need it.");
  cbox->onChange(signalSamplingButton, p_model);
  cbox->setChecked(R.random_sample);

  current = tgui::Label::create();
  current->setPosition("parent.left + 50", "parent.bottom - 100 - 20");
  current->setTextSize(14);
  pgui->add(current, "zconst_label");

  editBox = tgui::EditBox::create();
  editBox->setSize(100, 20);
  editBox->setTextSize(14);
  editBox->setPosition("parent.left + 50", "parent.bottom - 100");
  editBox->setDefaultText("0");
  pgui->add(editBox, "zconst_real_box");
  setTip(editBox, "Julia constant c, real part (Julia sets only)");
  editBox->onReturnOrUnfocus(signalZconstr, p_model, pgui);

  editBox = tgui::EditBox::create();
  editBox->setSize(100, 20);
  editBox->setTextSize(14);
  editBox->setPosition("parent.left + 50 + 120", "parent.bottom - 100");
  editBox->setDefaultText("0");
  pgui->add(editBox, "zconst_imag_box");
  setTip(editBox, "Julia constant c, imaginary part (Julia sets only)");
  editBox->onReturnOrUnfocus(signalZconsti, p_model, pgui);

  current = tgui::Label::create();
  current->setPosition("parent.left + 50", "parent.bottom - 80");
  current->setTextSize(14);
  pgui->add(current, "escape_r_label");

  editBox = tgui::EditBox::create();
  editBox->setSize(100, 20);
  editBox->setTextSize(14);
  editBox->setPosition("parent.left + 50 + 120", "parent.bottom - 60");
  editBox->setDefaultText("0");
  pgui->add(editBox, "escape_r_box");
  setTip(editBox, "Escape radius: an orbit that gets this far from 0 counts as escaped (default 2)");
  editBox->onReturnOrUnfocus(signal_escape_r, p_model, pgui);

  // Save Fractal Group

  auto button = tgui::Button::create();
  button->setPosition("parent.left + 400", "parent.bottom - 150");
  button->setText("Remember view");
  button->setSize(120, 30);
  pgui->add(button, "SaveFractal");
  setTip(button, "Remember this view in memory (up to 30, gone when the app exits)");
  button->onPress(signalSaveFractal, p_model, pgui);

  button = tgui::Button::create();
  button->setPosition("parent.left + 400", "parent.bottom - 100");
  button->setText("Next remembered");
  button->setSize(120, 30);
  pgui->add(button, "LoadNextSaved");
  setTip(button, "Go to the next remembered view");
  button->onPress(signalLoadNextSaved, p_model, pgui);

  current = tgui::Label::create();
  current->setPosition("parent.left + 400", "parent.bottom - 50");
  current->setTextSize(14);
  pgui->add(current, "saved_fractal_label");
  
  button = tgui::Button::create();
  button->setPosition("parent.left + 600", "parent.bottom - 150");
  button->setText("Save key file");
  button->setSize(120, 30);
  pgui->add(button, "SaveKey");
  setTip(button, "Save this view as a JSON key file in FractalsData/keys (see fractals --help)");
  button->onPress(signalSaveKey, p_model, pgui, "");

  button = tgui::Button::create();
  button->setPosition("parent.left + 600", "parent.bottom - 100");
  button->setText("Next key file");
  button->setSize(120, 30);
  pgui->add(button, "LoadNextKey");
  setTip(button, "Load the next key file from FractalsData/keys (in name order)");
  button->onPress(signalLoadNextKey, p_model, pgui);

  current = tgui::Label::create();
  current->setPosition("parent.left + 600", "parent.bottom - 50");
  current->setTextSize(14);
  pgui->add(current, "keys_label");

  // Palette Column
  auto lbox = tgui::ListBox::create();
  lbox->setPosition("parent.left + 800", "parent.bottom - 300");
  lbox->setSize(100.f, 290.f);
  for (auto e : NSR.color_names) {
    lbox->addItem(e);
  }

  pgui->add(lbox, "ColorBox");
  addHeading(pgui, "Outside coloring", "parent.left + 800", "parent.bottom - 340");
  addHeading(pgui, "Palette", "parent.left + 800", "parent.bottom - 320");
  setTip(lbox, "Palette for the outside of the set (points that escape)");
  lbox->onItemSelect(signalColorBox);

  lbox = tgui::ListBox::create();
  lbox->setPosition("parent.left + 900", "parent.bottom - 300");
  lbox->setSize(60.f, 155.f);
  for (auto e : NSR.color_cycle_size_names) {
    lbox->addItem(e);
  }

  pgui->add(lbox, "CycleBox");
  addHeading(pgui, "Band size", "parent.left + 900", "parent.bottom - 320");
  setTip(lbox, "Band size: how many iterations one pass through the palette spans\n"
               "(16: narrow bands, 1024: wide). Used by Bands and 3D light outside and\n"
               "Orbit bands inside; the UF16 palette always has 16 bands.\n"
               "3D light uses 256 steps: 512 and 1024 use only part of the palette there.");
  lbox->onItemSelect(signalColorCycleBox);

  cbox = tgui::CheckBox::create();
  cbox->setPosition("parent.left + 900", "parent.bottom - 142");
  cbox->setText("Mirror");
  cbox->setSize(22, 22);
  cbox->setChecked(R.reflect_palette);
  pgui->add(cbox, "Reflect");
  setTip(cbox, "Run the palette back and forth instead of jumping from its end to its start");
  cbox->onChange(signalButton);

  lbox = tgui::ListBox::create();
  lbox->setPosition("parent.left + 900", "parent.bottom - 96");
  lbox->setSize(100.f, 88.f);
  for (const char *name : outside_style_names) lbox->addItem(name);

  pgui->add(lbox, "CAlgoBox");
  addHeading(pgui, "Style", "parent.left + 900", "parent.bottom - 114");
  setTip(lbox, "How the outside is colored:\n"
               "Bands: by iteration count, cycling through the palette\n"
               "Smooth: continuous colors across the whole palette\n"
               "Image: from a picture in FractalsData/escape_images (n: next picture)\n"
               "3D light: shading, as if lit from one side");
  lbox->onItemSelect(signalCAlgoBox);

  // Interior Coloring Column
  lbox = tgui::ListBox::create();
  lbox->setPosition("parent.left + 1000", "parent.bottom - 300");
  lbox->setSize(100.f, 290.f);
  for (auto e : NSR.color_names) {
    lbox->addItem(e);
  }

  pgui->add(lbox, "IntColorBox");
  addHeading(pgui, "Inside coloring", "parent.left + 1000", "parent.bottom - 340");
  addHeading(pgui, "Palette", "parent.left + 1000", "parent.bottom - 320");
  setTip(lbox, "Palette for the inside of the set (points that never escape)");
  lbox->onItemSelect(signalIntColorBox);

  lbox = tgui::ListBox::create();
  lbox->setPosition("parent.left + 1100", "parent.bottom - 300");
  lbox->setSize(60.f, 155.f);
  for (auto e : NSR.color_cycle_size_names) {
    lbox->addItem(e);
  }

  pgui->add(lbox, "IntCycleBox");
  addHeading(pgui, "Band size", "parent.left + 1100", "parent.bottom - 320");
  setTip(lbox, "Band size: how many iterations one pass through the palette spans\n"
               "(16: narrow bands, 1024: wide). Used by Bands and 3D light outside and\n"
               "Orbit bands inside; the UF16 palette always has 16 bands.\n"
               "3D light uses 256 steps: 512 and 1024 use only part of the palette there.");
  lbox->onItemSelect(signalIntColorCycleBox);

  cbox = tgui::CheckBox::create();
  cbox->setPosition("parent.left + 1100", "parent.bottom - 142");
  cbox->setText("Mirror");
  cbox->setSize(22, 22);
  cbox->setChecked(RI.reflect_palette);
  pgui->add(cbox, "IntReflect");
  setTip(cbox, "Run the inside palette back and forth");
  cbox->onChange(signalIntButton);

  lbox = tgui::ListBox::create();
  lbox->setPosition("parent.left + 1200", "parent.bottom - 270");
  lbox->setSize(110.f, 130.f);
  for (const char *name : inside_style_names) lbox->addItem(name);

  pgui->add(lbox, "IntCAlgoBox");
  addHeading(pgui, "Style", "parent.left + 1200", "parent.bottom - 320");
  setTip(lbox, "How the inside is colored:\n"
               "Solid color: one color, set in the box above (0xBBGGRR)\n"
               "Orbit bands: by how far the orbit travels, through the palette\n"
               "Image: a picture from FractalsData/escape_images, by position\n"
               "Orbit angle: from where the orbit ends and how far it travels\n"
               "Orbit length: red/green/blue from how far the orbit travels\n"
               "Length waves: waves in the orbit length\n"
               "The box above scales the effect.");
  lbox->onItemSelect(signalIntCAlgoBox);


  editBox = tgui::EditBox::create();
  editBox->setSize(110, 20);
  editBox->setTextSize(14);
  editBox->setPosition("parent.left + 1200", "parent.bottom - 300");
  editBox->setDefaultText("dec triple");
  pgui->add(editBox, "interior_color_adjust");
  setTip(editBox, "Inside adjust. Solid color: the color as 0xBBGGRR, e.g. 0x0000ff is red\n"
                  "(decimal works too). Other styles: a scale factor. Enter to apply.");
  editBox->onReturnOrUnfocus(signalIntColorAdj, p_model, pgui);

  for (auto &w : pgui->getWidgets())
    if (auto label = std::dynamic_pointer_cast<tgui::Label>(w))
      label->getRenderer()->setBackgroundColor(tgui::Color(0, 0, 0, 150));

  pgui->add(menu);  // to be on top

}

// When model changes resulting in gui changes
void updateGuiElements(shared_ptr<tgui::Gui> &pgui,
                       shared_ptr<FractalModel> &p_model) {
  // hide all widgets
  // show and update the ones that are needed
  // labels
  // default values for gui elements
}

// When some per model control changes: f.e. theta
void setGuiElementsFromModel(shared_ptr<tgui::Gui> &pgui,
                             shared_ptr<FractalModel> &p_model) {
  auto select = [&](const char *name, int index) {
    auto box = pgui->get<tgui::ListBox>(name);
    if (box && box->getSelectedItemIndex() != index) box->setSelectedItemByIndex(index);
  };
  auto check = [&](const char *name, bool on) {
    auto box = pgui->get<tgui::CheckBox>(name);
    if (box && box->isChecked() != on) box->setChecked(on);  // the handlers set, not toggle
  };
  auto position = [](const auto &list, auto value) {
    for (size_t i = 0; i < std::size(list); ++i)
      if (list[i] == value) return (int)i;
    return -1;
  };
  select("ColorBox", static_cast<int>(R.palette));
  select("CycleBox", cycleIndex(R.color_cycle_size));
  select("CAlgoBox", position(outside_styles, R.color_algo));
  check("Reflect", R.reflect_palette);
  select("IntColorBox", static_cast<int>(RI.palette));
  select("IntCycleBox", cycleIndex(RI.color_cycle_size));
  select("IntCAlgoBox", position(inside_styles, RI.color_algo));
  check("IntReflect", RI.reflect_palette);
  check("RandomSample", R.random_sample);
  check("AutoIterations", R.auto_iterations);
}

// update current things every draw even if no change
void updateCurrentGuiElements(shared_ptr<tgui::Gui> &pgui,
                              shared_ptr<FractalModel> &p_model, float secs,
                              float fps) {
  refreshNumberBoxes(pgui, p_model);

  // One label per status row, parts separated by spaces, so longer values
  // push the rest along instead of running into the next label
  const SupportedFractal &f = FRAC[p_model->current_fractal];
  const std::string gap = "      ";

  std::string row1 = "Fractal: " + f.name;
  if (f.probabalistic)  // only the Buddhabrots accumulate hits
    row1 += gap + "Brightest pixel R/G/B: " + to_string(p_model->maxred) + "/" +
            to_string(p_model->maxgreen) + "/" + to_string(p_model->maxblue) +
            " hits, total " + formatCount((double)p_model->hitsums);
  auto current = pgui->get<tgui::Label>("fractal_label");
  current->setText(row1);

  std::string gpu;
  if (p_model->cuda_detected == false || !has_gpu_kernel(f))
    gpu = "GPU: n/a";  // no usable GPU (see the log), or no kernel for this fractal
  else if (!f.cuda_mode)
    gpu = "GPU: off (c)";
  else if (f.probabalistic || p_model->gpu_rendered)
    gpu = "GPU: on";
  else
    gpu = "GPU: starting";  // switched on, no GPU frame yet
  current = pgui->get<tgui::Label>("secs_label");
  current->setText("Time: " + to_string((int)secs) + " s" + gap +
                   "fps: " + to_string((int)(fps + 0.5f)) + gap +
                   "Samples/s: " +
                   formatCount((double)p_model->stats[p_model->current_fractal].samples_per_second) +
                   gap + "Threads: " + to_string(p_model->num_threads) + gap + gpu);

  std::string zoom_string;
  std::ostringstream out;
  out.precision(16);
  out << std::fixed << R.displayed_zoom;
  zoom_string = out.str();

  current = pgui->get<tgui::Label>("boundary_label");
  {
    std::string precision_text;
    const SupportedFractal &fr = FRAC[p_model->current_fractal];
    bool deep_capable = has_escape_kernel(fr) && fr.current_power == 2;
    double scale = std::max({std::fabs(R.xstart), std::fabs(R.ystart), 1e-300});
    if (deep_capable && p_model->deep_active) {
      // Short enough to stay left of the color lists; keys keep the exact center
      auto shorten = [](const std::string &v) { return v.size() > 24 ? v.substr(0, 23) + "..." : v; };
      current->setText("View: zoom " + formatNumber(R.displayed_zoom) + ", center " +
                       shorten(deep::center_x_str()) + ", " + shorten(deep::center_y_str()));
      precision_text = "Precision: deep zoom (perturbation)";
    } else {
      std::string precision = "Precision: double";
      if (!fr.probabalistic && R.xdelta < 1e-14 * scale)
        precision += deep_capable ? "" : " - at its limit (deep zoom needs Mandelbrot or Julia, power 2)";
      current->setText("View: x " + formatNumber(R.xstart) + " .. " +
                       formatNumber(R.xstart + (R.original_width) * R.xdelta) + ", y " +
                       formatNumber(R.ystart) + " .. " +
                       formatNumber(R.ystart + (R.original_height) * R.ydelta) + ", zoom " +
                       formatNumber(R.displayed_zoom));
      precision_text = precision;
    }
    // The precision goes on the shorter Time/GPU row
    auto time_row = pgui->get<tgui::Label>("secs_label");
    time_row->setText(time_row->getText() + "      " + precision_text);
  }

  {
    const SampleStats &st = p_model->stats[p_model->current_fractal];
    std::string escaped = formatPercent((double)st.escaped_set, (double)st.in_set + (double)st.escaped_set);
    current = pgui->get<tgui::Label>("stats_label");
    if (FRAC[p_model->current_fractal].probabalistic) {
      std::string skipped = formatPercent((double)st.rejected, (double)st.total);
      current->setText("Samples: " + formatCount((double)st.total) + ", skipped (known inside) " +
                       skipped + ", escaped " + escaped);
    } else {
      current->setText("Points computed: " + formatCount((double)st.total) + ", escaped " + escaped);
    }
  }

  // Params column
  // The boxes show the values; the labels name them
  current = pgui->get<tgui::Label>("power_label");
  current->setText("Power");

  if (auto random_box = pgui->get<tgui::CheckBox>("RandomSample")) {
    bool anti = FRAC[p_model->current_fractal].anti;
    if (random_box->isEnabled() == anti) random_box->setEnabled(!anti);
  }

  if (auto autobox = pgui->get<tgui::CheckBox>("AutoIterations")) {
    const SupportedFractal &fr = FRAC[p_model->current_fractal];
    // Only Mandelbrot and Julia have auto iterations; elsewhere the checkbox is
    // hidden (the Buddhabrot label uses its row for "red, green, blue")
    auto power_box = pgui->get<tgui::EditBox>("power_box");  // hidden with the GUI (g key)
    bool applies = has_escape_kernel(fr) && power_box && power_box->isVisible();
    if (autobox->isVisible() != applies) autobox->setVisible(applies);
    std::string text = "Auto (" + to_string(effective_iters(fr)) + ")";
    if (applies && autobox->getText() != text) autobox->setText(text);
  }

  current = pgui->get<tgui::Label>("miters_label");
  current->setText(FRAC[p_model->current_fractal].probabalistic ? "Max iterations\n(red, green, blue)"
                                                                 : "Max iterations");

  current = pgui->get<tgui::Label>("zconst_label");
  current->setText("Julia constant (real, imaginary)");

  current = pgui->get<tgui::Label>("escape_r_label");
  current->setText("Escape radius");

  current = pgui->get<tgui::Label>("saved_fractal_label");
  current->setText(displayed_frac_ix < 0 ? "Remembered: none shown"
                                         : "Remembered view " + to_string(displayed_frac_ix + 1));

  current = pgui->get<tgui::Label>("keys_label");
  current->setText(last_loaded_key_ix < 0 ? "Key files: " + to_string(key_count)
                                          : "Key file " + to_string(last_loaded_key_ix + 1) +
                                                " of " + to_string(key_count) + ":\n" +
                                                (loaded_key_name.size() > 24
                                                     ? loaded_key_name.substr(0, 23) + "..."
                                                     : loaded_key_name));
}

void display_all_widgets(shared_ptr<tgui::Gui> &pgui, bool maybe) {
  std::vector<tgui::Widget::Ptr> widgets;
  widgets = pgui->getWidgets();

  for (auto &w : widgets) {
    w->setVisible(maybe);
  }
}

// respond to mouse wheel zoom
double get_new_zoom(sf::View &view, int delta) {
  if (delta < 0) {
    // zoom in
    R.requested_zoom = R.requested_zoom * 0.90;
    // cout << "zoom: " << current_zoom << endl;
    if (R.requested_zoom < 0.000000001) {
      R.requested_zoom = 1.0;
    }
  } else {
    // zoom out
    R.requested_zoom = R.requested_zoom * 1.1;
    // cout << "zoom: " << current_zoom << endl;

    if (R.requested_zoom > 25.0) {
      R.requested_zoom = 1.0;
    }
  }

  return R.requested_zoom;
}

// save a screenshot
void save_screenshot(sf::RenderWindow &window, string name, sf::View &modelview,
                     shared_ptr<FractalModel> p_model,
                     shared_ptr<tgui::Gui> pgui, bool display_gui,
                     std::string savename) {
  char buffer[80] = "no date";
  time_t rawtime;
  struct tm *timeinfop = nullptr;

  time(&rawtime);
#ifdef _WIN32
  struct tm timeinfo;
  localtime_s(&timeinfo, &rawtime);
  timeinfop = &timeinfo;
#else
  timeinfop = localtime(&rawtime);
#endif

  strftime(buffer, sizeof(buffer), "%d-%m-%Y_%H_%M_%S", timeinfop);
  std::string timestring(buffer);

  // window.clear();
  // window.setView(modelview);
  window.draw(*p_model);  // draw fractals in case the model is being hidden
  if (display_gui) pgui->draw();

  sf::Vector2u windowSize = window.getSize();
  sf::Texture texture;
  if (!texture.resize(windowSize)) {
    // e.g. Windows' built-in OpenGL 1.1 (no GPU driver) is limited to 1024x1024
    cout << "Failed to save screenshot: OpenGL can't make a " << windowSize.x << "x"
         << windowSize.y << " texture (limit " << sf::Texture::getMaximumSize() << ")" << endl;
    return;
  }
  texture.update(window);
  sf::Image screenshot = texture.copyToImage();
  std::string outname = (savename != "none")
                            ? savename
                            : (data_dir / "screenshots").string() + separator + name +
                                  timestring + ".png";
  if (!screenshot.saveToFile(outname))
    cout << "Failed to save screenshot: " << outname << endl;
};

const char *usage_text = R"(Usage: fractals [options]

Interactive fractal explorer: Mandelbrot, Julia, Buddhabrot, Newton, Nova, ...

Options:
  --windowed      open in a window (3/4 of the screen, at the left edge) instead
                  of borderless full screen
  --console       show the log in a console (Windows; on Linux it always prints)
  --cuda          start with the GPU on for every fractal that has a GPU kernel
                  (Mandelbrot, Julia, and Buddhabrot with z^2); the c key toggles it
  --no-cuda       never use the GPU
  --threads <n>   number of render threads (default: CPU threads - 1, at most 32)
  --save-and-exit <key.json> <out.png>
                  render the fractal key (JSON) at 2560x1440, save the image as
                  out.png, write the key it rendered to changed_key.json, and exit.
                  Paths are relative to the current folder.
  --hide          with --save-and-exit: keep the window hidden while rendering
  -h, --help      show this help and exit

Example:
  fractals --save-and-exit my_key.json my_key.png --hide

Folders:
  FractalsData/ is created next to the executable. If that folder isn't
  writable, the per-user folder is used instead: %LOCALAPPDATA%\Fractals on
  Windows, $XDG_DATA_HOME/fractals or ~/.local/share/fractals on Linux.
    keys/           fractal keys (*.json): "Save Key" writes one here,
                    "Load Next Key" loads them in name order
    screenshots/    images saved with the s key
    escape_images/  images for the USE_IMAGE coloring; the n key cycles through
                    them. Add your own .jpg/.png files here.
    themes/         GUI themes
    fractals.log    the log of the last run
  The defaults for themes/ and escape_images/ are built into the executable and
  written out only when missing, so edits and additions are kept.

Fractal keys (JSON):
  "auto_iterations": true raises max_iterations with the zoom (Mandelbrot and
  Julia; keys without it use max_iterations as is).
  A key describes one image: the fractal, its parameters, the view, coloring
  and lighting. Every field except "fractal" is optional (a missing field gets
  its default) and unknown fields are ignored. Names are used for enums.
  Example:
    {
      "format_version": 1,
      "fractal": "Mandelbrot_1000",
      "max_iterations": [1000, 0, 0],
      "power": 2.0,
      "zconst": [0.0, 0.0],
      "escape_radius": 2.0,
      "view": { "x_start": -0.7636, "y_start": 0.1178, "zoom": 0.01,
                "requested_zoom": 0.01 },
      "coloring": { "algo": "SMOOTH", "palette": "Viridis", "cycle_size": 32,
                    "reflect_palette": false },
      "lighting": { "pos_r": 1.0, "pos_i": 0.0, "angle": 45.0, "height": 1.5 },
      "interior": { "algo": "SOLID", "palette": "UF16", "cycle_size": 256 }
    }
  fractal:    a name from the Fractal menu (e.g. Julia, Buddhabrot)
  view:       center_x/center_y (strings) are the exact view center; deep zooms need
              more digits than a double has. Without them, x_start/y_start (the
              top-left corner, as doubles) place the view.
              x_start/y_start is the top-left corner in fractal coordinates;
              zoom 1 shows the fractal's full default range, smaller zooms in.
              When requested_zoom differs, the view zooms about its center.
  coloring:   algo MULTICYCLE, SMOOTH, USE_IMAGE or SHADOW_MAP; palette Parula,
              Heat, Jet, Turbo, Hot, Gray, Magma, Inferno, Plasma, Viridis,
              Cividis, Github, Cubehelix or UF16
  interior:   algo SOLID, MULTICYCLE, USE_IMAGE, TRIG, DIST or DIST2
  tools/make_fractal_movies.py animates a key into a GIF and MP4s.

Deep zoom: past about zoom 1e-10, Mandelbrot and Julia (power 2) switch to
perturbation (one high-precision reference orbit, pixels as small offsets from
it) and zoom on to about 1e-150. The status line shows "Precision: deep zoom".

While running: the Help menu lists the keys (c: CUDA on/off, s: screenshot,
z: undo zoom, e: exit, ...). The mouse wheel zooms, the right button recenters
and dragging with the left button zooms to the selection.
)";

// Map a window pixel to image coordinates (they differ when windowed)
sf::Vector2i windowToImage(const sf::RenderWindow &window, sf::Vector2i pos) {
  sf::Vector2u size = window.getSize();
  return sf::Vector2i(pos.x * IMAGE_WIDTH / (int)size.x,
                      pos.y * IMAGE_HEIGHT / (int)size.y);
}

int main(int argc, char **argv) {
  std::vector<std::string> argList;
  std::string savename{"no key"};
  std::string keyname{"no key"};
  update_and_draw = false;
  save_and_exit = false;
  hide = false;

  bool console = false;
  bool windowed = false;
  bool help = false;
  int cuda_option = 0;  // +1 --cuda, -1 --no-cuda
  unsigned int requested_threads = 0;  // 0: one per CPU thread, minus one
  std::string error;
  for (int i = 1; i < argc && error.empty(); ++i) {
    std::string arg = argv[i];
    argList.push_back(arg);
    // The option's value(s): the next argument(s), which must exist
    auto value = [&](const char *what) -> std::string {
      if (i + 1 >= argc) {
        error = arg + " needs " + what;
        return "";
      }
      argList.push_back(argv[i + 1]);
      return argv[++i];
    };
    if (arg == "--console")
      console = true;
    else if (arg == "--windowed")
      windowed = true;
    else if (arg == "--cuda")
      cuda_option = 1;
    else if (arg == "--no-cuda")
      cuda_option = -1;
    else if (arg == "--hide")
      hide = true;
    else if (arg == "--threads") {
      std::string n = value("a number");
      char *end = nullptr;
      long v = error.empty() ? std::strtol(n.c_str(), &end, 10) : 0;
      if (error.empty() && (n.empty() || *end != '\0' || v < 1))
        error = "--threads needs a number of 1 or more, not \"" + n + "\"";
      requested_threads = (unsigned int)std::max(0L, v);
    } else if (arg == "--save-and-exit") {
      keyname = value("a key file and an output PNG");
      if (error.empty()) savename = value("an output PNG after the key file");
      save_and_exit = error.empty();
    } else if (arg == "-h" || arg == "--help")
      help = true;
    else if (arg.size() > 1 && arg[0] == '-')
      error = "unknown option: " + arg;
    else
      error = "unexpected argument: " + arg;
  }
  if (error.empty() && hide && !save_and_exit) error = "--hide only works with --save-and-exit";

  if (help || !error.empty()) {
    runtime::openConsole();  // the Windows exe has no console of its own
    if (!error.empty()) cout << error << "\n\n";
    cout << usage_text;
    cout.flush();
    return error.empty() ? 0 : 2;
  }

  if (console) runtime::openConsole();
  data_dir = runtime::setupDataDir();
  runtime::startLogging(data_dir / "fractals.log", console);
  runtime::extractDefaultAssets(data_dir);
  keys_location = (data_dir / "keys").string() + separator;
  escape_dir = (data_dir / "escape_images").string();
  cout << "Data folder: " << data_dir.string() << endl;

  cout << "Command line: " << argv[0];
  for (auto &val : argList) cout << " " << val;
  cout << endl;
  // --save-and-exit renders one key, saves the image and exits; the movie
  // script edits keys and calls it once per frame

  // Register signal and signal handler
  signal(SIGINT, signal_callback_handler);

  if (!save_and_exit) {
    sf::Vector2u desktop = sf::VideoMode::getDesktopMode().size;
    IMAGE_WIDTH = (int)desktop.x;
    IMAGE_HEIGHT = (int)desktop.y;
  }
  cout << "Render size: " << IMAGE_WIDTH << "x" << IMAGE_HEIGHT << endl;

  sf::Vector2u screenDimensions(IMAGE_WIDTH, IMAGE_HEIGHT);
  sf::RenderWindow window;
  if (windowed && !save_and_exit) {
    // --windowed: a 3/4-size window at the left edge, so the rest of the
    // desktop stays visible. The fractal is still rendered at the full size
    // and scaled down; the GUI keeps its native pixel size so text stays
    // readable.
    window.create(sf::VideoMode(sf::Vector2u(screenDimensions.x * 3 / 4, screenDimensions.y * 3 / 4)),
                  "Fractals!", sf::Style::Titlebar | sf::Style::Close);
    window.setPosition(sf::Vector2i(0, 0));
  } else {
    // Borderless window covering the whole screen. F switches to exclusive fullscreen.
    window.create(sf::VideoMode(sf::Vector2u(screenDimensions.x, screenDimensions.y)),
                  "Fractals!", sf::Style::None);
    window.setPosition(sf::Vector2i(0, 0));
  }
  if (hide) window.setVisible(false);

  window.setKeyRepeatEnabled(false);

  {
    sf::ContextSettings gl = window.getSettings();
    cout << "OpenGL " << gl.majorVersion << "." << gl.minorVersion << ", max texture size "
         << sf::Texture::getMaximumSize() << endl;
  }

  // // Display the list of all the video modes available for fullscreen
  // std::vector<sf::VideoMode> modes = sf::VideoMode::getFullscreenModes();
  // for (std::size_t i = 0; i < modes.size(); ++i)
  // {
  //   sf::VideoMode mode = modes[i];
  //   std::cout << "Mode #" << i << ": "
  //             << mode.width << "x" << mode.height << " - "
  //             << mode.bitsPerPixel << " bpp" << std::endl;
  // }
  // // Create a window with the same pixel depth as the desktop
  // sf::VideoMode desktop = sf::VideoMode::getDesktopMode();
  // window.create(sf::VideoMode(1024, 768, desktop.bitsPerPixel), "SFML
  // window");

  sf::View modelview;
  sf::Vector2u viewD(screenDimensions.x, screenDimensions.y);
  modelview.setSize(sf::Vector2f((float)viewD.x, (float)viewD.y));
  R.displayed_zoom = 1.0;
  R.requested_zoom = 1.0;
  R.current_height = screenDimensions.y;
  R.current_width = screenDimensions.x;
  R.original_height = screenDimensions.y;
  R.original_width = screenDimensions.x;

  R.color_cycle_size = 32;
  R.palette =
      tinycolormap::ColormapType::UF16;  // tinycolormap::ColormapType::Viridis;
                                         // tinycolormap::ColormapType::UF16
  R.color_algo = ColoringAlgo::MULTICYCLE;

  std::string escape_file1 =
      escape_dir + separator + std::string("escape_image.jpg");
  std::string escape_file2 =
      escape_dir + separator + std::string("escape_image.png");
  // R.color_algo = ColoringAlgo::USE_IMAGE;
  if (!load_escape_image(escape_file1) && !load_escape_image(escape_file2)) {
    cout << "missing escape_image.jpg[png] for fractal escape coloring" << endl;
    R.color_algo = ColoringAlgo::MULTICYCLE;
    R.image_loaded = false;
  }
  R.light_pos_r = 1;
  R.light_pos_i = 0;
  R.light_angle = 45;
  R.light_height = 1.5;

  modelview.setCenter(sf::Vector2f((float)screenDimensions.x / 2.0f, (float)screenDimensions.y / 2.0f));
  window.setView(modelview);

  // create the fractal model (i.e. Model)
  //    note: we are passing this shared ptr to signals and threads so they can
  //    change the model (MVC)
  auto p_model =
      make_shared<FractalModel>(screenDimensions.x, screenDimensions.y);

  // p_model->cudaTest();
  if (cuda_option < 0)
    cout << "CUDA: turned off by --no-cuda, using CPU threads" << endl;
  else
    p_model->cudaPresent();
  if (cuda_option > 0)
    for (auto &frac : FRAC)
      if (has_gpu_kernel(frac)) frac.cuda_mode = true;

  // Create the worker threads:
  cout << "Machine supports " << thread::hardware_concurrency()
       << " simultaneous threads" << endl;

  // --threads, or one per CPU thread minus one for the GUI; at most MAX_THREADS
  // (the per-thread arrays' size)
  unsigned int hw_threads = std::max(2u, thread::hardware_concurrency());
  unsigned int num_threads = requested_threads ? requested_threads : hw_threads - 1;
  num_threads = std::min<unsigned int>(num_threads, MAX_THREADS);
  p_model->num_threads = num_threads;

  cout << "Using " << num_threads << " threads to speed up fractal rendering"
       << endl;
  thread threads[MAX_THREADS];
  std::promise<void> terminateThreadSignal[MAX_THREADS];

  // start up thread pool
  // thread:
  // input: thread id, p_model, mutex to report results, future object for
  // thread termination output: merges hits into model under a mutex
  for (unsigned int tix = 0; tix < num_threads; ++tix) {
    std::future<void> futureObj = terminateThreadSignal[tix].get_future();
    
    threads[tix] = thread(&FractalModel::fractal_thread, p_model, tix,
                          std::move(futureObj), &thread_asked_to_reset[0],
                          &thread_iteration[0], &update_and_draw);
  }

  bool display_gui = true;
  bool display_fractal = true;
  auto pgui = make_shared<tgui::Gui>(window);
  tgui::Theme::setDefault((data_dir / "themes" / "Black.txt").string());
  createGuiElements(pgui, p_model);
  updateGuiElements(pgui, p_model);

  // load the fractal key on startup
  if (save_and_exit) {
    if (LoadProvidedKey(p_model, pgui, keyname)) {
      // terminate threads in thread pool
      for (unsigned int tix = 0; tix < num_threads; ++tix) {
        terminateThreadSignal[tix].set_value();
        // to make it check for terminate
        for (unsigned int tix = 0; tix < num_threads; ++tix) {
          thread_asked_to_reset[tix] = true;
        }
        threads[tix].join();
      }
      exit(-1);
    }
  }
  for (unsigned int tix = 0; tix < num_threads; ++tix) {
    thread_asked_to_reset[tix] = true;
  }
  update_and_draw = true;

  // Track attempted crops with mouse
  bool cropping = false;  // a left-button drag that started on the fractal
  int crop_start_x = 0;
  int crop_start_y = 0;
  int crop_end_x = 0;
  int crop_end_y = 0;
  sf::RectangleShape selection(sf::Vector2f(0, 0));
  R.show_selection = false;

  // create a clock to track the elapsed time
  sf::Clock clock_s;  // start
  sf::Clock clock_e;  // elapsed between each draw cycle

  sf::Time start = clock_s.restart();
  int frames = 0;

  while (window.isOpen()) {
    while (const std::optional event = window.pollEvent()) {
      if (event->is<sf::Event::Closed>()) window.close();  // breaks out above

      // The GUI sees every event first and says whether a widget took it
      bool gui_took_event = pgui->handleEvent(*event);
      // While a number box has focus, keys are text: typing "1e-3" must not
      // press e (exit), c (CUDA), s (screenshot), ...
      bool typing = std::dynamic_pointer_cast<tgui::EditBox>(pgui->getFocusedLeaf()) != nullptr;

      // Handle keyboard control commands

      if (const auto* keyPressed = typing ? nullptr : event->getIf<sf::Event::KeyPressed>()) {
        if (keyPressed->scancode == sf::Keyboard::Scancode::G) {
          if (display_gui == false) {
            display_gui = true;
            display_all_widgets(pgui, true);
          } else {
            display_gui = false;
            display_all_widgets(pgui, false);
          }
        }

        if (keyPressed->scancode == sf::Keyboard::Scancode::H) {
          if (display_fractal == false) {
            display_fractal = true;
          } else {
            display_fractal = false;
          }
        }

        if (keyPressed->scancode == sf::Keyboard::Scancode::Z) {
          update_and_draw = false;
          cout << " Z update paused " << endl;

          for (unsigned int tix = 0; tix < num_threads; ++tix) {
            thread_asked_to_reset[tix] = true;
          }
          LoadLast(p_model, pgui);
          update_and_draw = true;
        }

        if (keyPressed->scancode == sf::Keyboard::Scancode::P) {
          if (update_and_draw == false) {
            update_and_draw = true;
            frames = 0;
            start = clock_s.restart();
          } else {
            update_and_draw = false;
            cout << " P update paused " << endl;

            frames = 0;
            start = clock_s.restart();
          }
        }

        if (keyPressed->scancode == sf::Keyboard::Scancode::S) {
          save_screenshot(window, FRAC[p_model->current_fractal].name,
                          modelview, p_model, pgui, display_gui, "none");
        }

        if (keyPressed->scancode == sf::Keyboard::Scancode::E) {
          window.close();
          break;
        } 
        if (keyPressed->scancode == sf::Keyboard::Scancode::N) {
          update_and_draw = false;
          cout << " N update paused " << endl;

          for (unsigned int tix = 0; tix < num_threads; ++tix) {
            thread_asked_to_reset[tix] = true;
          }
          signalLoadNextEscape(p_model, pgui);
          update_and_draw = true;
        }

        if (keyPressed->scancode == sf::Keyboard::Scancode::C) {
          if (FRAC[p_model->current_fractal].cuda_mode == true)
            FRAC[p_model->current_fractal].cuda_mode = false;
          else
            FRAC[p_model->current_fractal].cuda_mode = true;
        } 

        if (keyPressed->scancode == sf::Keyboard::Scancode::F) {
          // sf::VideoMode desktop = sf::VideoMode::getDesktopMode();
          // window.setSize(sf::Vector2u{screenDimensions.x,
          // screenDimensions.y});
          window.create(sf::VideoMode(sf::Vector2u(screenDimensions.x, screenDimensions.y)),
                        "Fractals!", sf::State::Fullscreen);
          window.setSize(sf::Vector2u{screenDimensions.x, screenDimensions.y});
        }
      } //keypressed
      

        // Handle Mouse (only events no GUI widget took: scrolling a list or
        // clicking a button must not zoom, pan or crop the fractal)
        // Zoom the whole sim if mouse wheel moved
        if (const auto* scrollEvent = gui_took_event ? nullptr : event->getIf<sf::Event::MouseWheelScrolled>()) {
            SaveLast(p_model);
            double newzoom =
                get_new_zoom(modelview, (int)scrollEvent->delta);
            cout << "New zoom: " << newzoom << endl;
            p_model->zoomFractal(newzoom);
            for (unsigned int tix = 0; tix < num_threads; ++tix) {
              thread_asked_to_reset[tix] = true;
            }
        }

        // record center for right mouse button and crop for left
        if (const auto* mouseButton = gui_took_event ? nullptr : event->getIf<sf::Event::MouseButtonPressed>()) {
            if (mouseButton->button == sf::Mouse::Button::Right) {
            // Pan
            SaveLast(p_model);
            cout << "New center: ";
            sf::Vector2i pos = windowToImage(window, mouseButton->position);
            cout << "x: " << pos.x;
            cout << " y: " << pos.y << endl;
            p_model->panFractal(pos.x, pos.y);
            for (unsigned int tix = 0; tix < num_threads; ++tix) {
              thread_asked_to_reset[tix] = true;
            }
            }

            if (mouseButton->button == sf::Mouse::Button::Left) {
            // Crop start
            SaveLast(p_model);
            sf::Vector2i pos = windowToImage(window, mouseButton->position);
            crop_start_x = pos.x;
            crop_start_y = pos.y;
            cropping = true;
            }
        }

        // Crop finish: only for a crop that started on the fractal
        if (const auto* mouseButton = event->getIf<sf::Event::MouseButtonReleased>()) {
            if (mouseButton->button == sf::Mouse::Button::Left && cropping) {
            cropping = false;
            sf::Vector2i pos = windowToImage(window, mouseButton->position);
            crop_end_x = pos.x;
            crop_end_y = pos.y;

            // A plain click (no drag) is not a crop
            if (abs(crop_end_x - crop_start_x) > 10) {
              cout << "maintaining aspect ratio" << endl;
              // We cant do this directly - we have to combine pan and zoom
              // since they preserve aspect ratio
              p_model->panFractal((crop_start_x + crop_end_x) / 2,
                                  (crop_start_y + crop_end_y) / 2);
              // Now fake a new zoom -> update R.requested_zoom
              R.requested_zoom =
                  R.requested_zoom *
                  (abs(crop_end_x - crop_start_x) / R.original_width);
              p_model->zoomFractal(R.requested_zoom);
              R.show_selection = false;
              // tell threads to start drawing new stuff
              for (unsigned int tix = 0; tix < num_threads; ++tix) {
                thread_asked_to_reset[tix] = true;
              }
            }
            R.show_selection = false;
          }
        }

        // Draw selection while button not released
        if (event->is<sf::Event::MouseMoved>() && cropping) {

            const auto& mouseMove = event->getIf<sf::Event::MouseMoved>();
            sf::Vector2i movePos = windowToImage(window, mouseMove->position);
          // Show the rectangle once the drag is wide enough to be a crop
          if (abs(movePos.x - crop_start_x) > 10) {
            selection.setSize(
                sf::Vector2f(abs((float)crop_start_x - movePos.x),
                             abs((float)crop_start_y - movePos.y)));
            selection.setFillColor(sf::Color::Transparent);
            selection.setPosition(sf::Vector2f((float)crop_start_x, (float)crop_start_y));

            // set a 5-pixel wide orange outline
            selection.setOutlineThickness(5);
            selection.setOutlineColor(sf::Color(250, 150, 100));
            R.show_selection = true;
          }
        }

      }
      ++frames;

      if (update_and_draw) {
        // Evolve the model independantly
        sf::Time elapsed = clock_e.restart();
        p_model->update(
            elapsed);  // rebuild the pixels from what threads did in background

        // Draw the GUI and the MODEL both of which are controlled by
        // Keyboard, mouse, and gui elements
        updateCurrentGuiElements(pgui, p_model, start.asSeconds(),
                                 frames / start.asSeconds());

        start = clock_s.getElapsedTime();

        window.clear();
        window.setView(modelview);
        if (display_fractal == true)
          window.draw(*p_model);  // draw fractals in gui off mode to save cpu
        if (R.show_selection) window.draw(selection);  // draw mouse selection
        pgui->draw();                                  // Draw all GUI widgets
        window.display();  // if you always do this it will cause screen jitter,
                           // but if you alt-tabe you will get white screen
      }

      bool done = true;
      for (unsigned int tix = 0; tix < num_threads; ++tix) {
        if (thread_iteration[tix] < 2) {
          done = false;
          break;
        }
      }

      if ((save_and_exit) && (done)) {
        // Evolve the model independantly
        sf::Time elapsed = clock_e.restart();
        p_model->update(
            elapsed);  // rebuild the pixels from what threads did in background

        {
          // Frame time: the slowest render thread's slice of the last pass
          double frame_ms = 0;
          for (unsigned int tix = 0; tix < num_threads; ++tix)
            frame_ms = std::max(frame_ms, thread_frame_ms[tix].load());
          cout << "saving " << savename << " (frame " << (int)frame_ms << " ms on "
               << (p_model->gpu_rendered ? "GPU" : "CPU") << ")" << endl;
        }
        // save screenshot
        save_screenshot(window, FRAC[p_model->current_fractal].name, modelview,
                        p_model, pgui, false, savename);
        signalSaveKey(p_model, pgui, "changed_key");
        window.close();
        break;
      }
    }

    // terminate threads in thread pool
    for (unsigned int tix = 0; tix < num_threads; ++tix) {
      terminateThreadSignal[tix].set_value();
      // to make it check for terminate
      for (unsigned int tix = 0; tix < num_threads; ++tix) {
        thread_asked_to_reset[tix] = true;
      }
      threads[tix].join();
    }
    // cout << "joined threads" << endl;

    return 0;
}
