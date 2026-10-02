# Ideas

Possible features for later, not yet planned. ★ = best payoff for the effort, given the
app as it is now. The suggested order to start with is: zoom toward the cursor,
progressive rendering, single precision on the GPU, high-resolution export. Several of
these touch the 3,600-line `fractals_with_gui_cuda.cpp`; splitting it (M9 in GOALS.md)
would make them cleaner to add, but M9 needs the user's go-ahead first.

## Exploring

- ★ **Zoom toward the cursor.** The mouse wheel zooms on the window's center, so you have
  to right-click first to recenter. Zooming toward the point under the mouse is what
  every fractal explorer does.
- ★ **Julia preview.** Hovering over a Mandelbrot point shows that point's Julia set in a
  corner. Each Julia set corresponds to a point of the Mandelbrot set, which makes this
  a good way to explore.
- **Smooth animated zoom and pan** instead of one jump per wheel notch, and **redo**
  alongside the existing undo (`z`).
- **Key gallery.** Browse saved keys with thumbnails instead of only "Next key file".
- **Auto-dive.** Find a minibrot or a high-detail spot and zoom in on it automatically,
  with the Newton's-method search used to make `tests/deep/minibrot_p18_2e-20.json`.

## Speed

- ★ **Single precision on the GPU for shallow zooms.** Consumer GPUs run 32-bit floats
  about 64x faster than 64-bit. Below zoom ~1e-5 that precision is enough, so most
  viewing would be far faster. It fits the regime switching that deep zoom started:
  float, then double, then deep.
- ★ **Progressive rendering.** Draw a coarse image first, then refine it, so there is
  feedback at once instead of waiting for whole frames at high iteration counts.
- **BLA for deep zoom.** Bilinear approximation skips thousands of iterations at a time.
  It is the standard way to make 1e-20 to 1e-100 frames take under a second instead of
  the current 14-22 s.
- **Reuse pixels when panning.** Only compute the strip that moved into view.

## Image quality and output

- ★ **High-resolution export.** Render 8K or 16K posters in tiles, beyond the screen
  size, with anti-aliasing (supersampling).
- **Keys inside PNGs.** Store the key in the PNG's metadata, so any saved image can be
  reopened at exactly that view.
- **Better coloring:**
  - distance estimation, for crisp boundary lines
  - histogram coloring, so colors spread evenly at any zoom
  - orbit traps and stripe-average coloring
- **Palette editor and animated palette cycling.**

## More fractals

- **Quick additions:** Burning Ship, Tricorn, Phoenix and Magnet. Each is a few lines in
  the existing escape-time path and would get the GPU and the coloring for free.
- **Larger projects:**
  - Lyapunov fractals
  - Newton fractals for any polynomial the user types in
  - a zoomable Buddhabrot
  - flame and IFS fractals
  - a 3D Mandelbulb, ray-marched on the GPU (a project of its own)

## Animation

- **Built-in recording.** Keyframes and MP4 output inside the app: a GUI over what
  `tools/make_fractal_movies.py` does.
- **Deep zoom movies** with smooth easing and deep precision.

## Usability and platforms

- **Remember the last session:** view, colors and window mode.
- **Shareable keys.** Copy and paste a key as a short text string.
- **Resizable windows.** Let `--windowed` windows be resized, rendering at the window's
  real size.
- **macOS build and code signing.** A CPU-only Mac build is mostly a CI job; code
  signing would remove the Windows SmartScreen warning.
- **Web version (WebGPU),** like the strange_attractor_visualizer rebuild.
