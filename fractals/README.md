# fractals

A fractal explorer written in C++ with SFML, TGUI and CUDA. It renders Mandelbrot, Julia,
Newton, Nova, Septagon, Buddhabrot and anti-Buddhabrot with threads on every CPU core and
uses CUDA when an NVIDIA GPU is present.

Ported from [pmiloslavsky/demo/fractals](https://github.com/pmiloslavsky/demo/tree/master/fractals)
(source commit `4e55d14`). The goal is a single self-contained executable for Windows and
Linux. The plan and milestones are in [GOALS.md](GOALS.md).

## Layout

| Path | Contents |
|---|---|
| `src/` | Application and CUDA kernel sources |
| `assets/themes/` | TGUI themes (embedded in the exe) |
| `assets/escape_images/` | Images for "use image" coloring (embedded in the exe) |
| `tools/` | `make_fractal_movies.py`, which renders a movie from a JSON key with the app (see its docstring) |

Build: `scripts/build-windows.ps1` or `scripts/build-linux.sh` (see GOALS.md). Run
`fractals --help` for the command-line options, folders and the JSON key format.

## Performance

Measured at 2560x1440 on an AMD Ryzen 9 9900X (12 cores, 23 render threads) and an
NVIDIA RTX 5070, Windows 11, release build. "Frame" is the time the slowest render thread
takes for its slice of the picture, from `save_and_exit` (see the log). The keys are in
`tests/benchmark/`; each was rendered with `--no-cuda` and `--cuda`.

| View | Max iterations | CPU | GPU | GPU speedup |
|---|---:|---:|---:|---:|
| Full Mandelbrot | 1000 | 352 ms | 128 ms | 2.8x |
| Seahorse valley, zoom 1e-2, smooth coloring | 1000 | 714 ms | 283 ms | 2.5x |
| Same, shadow map | 1000 | 863 ms | 399 ms | 2.2x |
| Julia (default) | 300 | 80 ms | 23 ms | 3.5x |
| Zoom 1e-6 | 3000 | 2572 ms | 1052 ms | 2.4x |
| Zoom 1e-10 | 10000 | 1495 ms | 762 ms | 2.0x |
| Zoom 1e-13 | 20000 | 3352 ms | 1849 ms | 1.8x |

The GPU and CPU images are identical, pixel for pixel, for all of these keys.

What the numbers come from:

- **Double precision.** Deep zooms need 64-bit floats, and consumer GPUs run those at
  1/64 of their 32-bit speed. So the GPU's advantage over 12 fast CPU cores is a few
  times, not a hundred.
- **Only the orbits run on the GPU.** One GPU thread per pixel iterates z → z² + c. The
  coloring (palettes, smooth coloring, shadow map, interior styles) stays on the CPU,
  shared with the CPU path, so both give the same picture. The CPU coloring is a good part
  of the GPU frame time.
- **Exact z².** `std::pow(z, 2)` computes exp(2·log z), which is slow and loses a few
  bits per step. The GPU and CPU disagreed on up to 63% of the pixels at zoom 1e-13
  because their `log`/`exp` round differently. Integer powers now use plain complex
  multiplication, which is exact IEEE arithmetic (with fused multiply-add turned off in the
  CUDA build). That made the images match and the CPU 3.6–8.5x faster. Non-integer powers
  still use `pow` and may differ slightly at the boundary.
- **Windows GPU timeout.** Windows resets a GPU that is busy for about 2 s. The work is
  sent in column tiles, sized from the measured speed so that a tile stays around 400 ms
  even if every pixel ran to max iterations.
- **Heap contention** made the first GPU version slower than the CPU. The default
  palette table was built with 17 heap allocations per pixel, and the 23 threads fought
  over the heap lock. With the orbits on the GPU, coloring is nearly all the CPU does,
  so this cost up to 1 s per frame. A constant table took the full view from about 1 s to
  128 ms on the GPU (and from 487 to 352 ms on the CPU). Threads waiting for the GPU also
  sleep now instead of spinning (`cudaDeviceScheduleBlockingSync`), so they don't take
  CPU time from the threads that are coloring.
- **Buddhabrot** uses random sampling on the GPU. The rewritten kernel does about 195k
  samples/s against 84k before M5, mostly from using all 48 multiprocessors.

The GPU is on by default for Mandelbrot, Julia, Buddhabrot and Buddhabrot_BW when an NVIDIA GPU
and driver are present. `c` toggles it per fractal, and `--no-cuda` turns it off. The
Buddhabrot kernel only does z² + c, so the Julia, anti and other-power variants always
run on the CPU ("Cuda N/A").

`src/tinycolormap.hpp` is © Yuki Koyama, MIT License.
