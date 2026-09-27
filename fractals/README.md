# fractals

A fractal explorer written in C++ with SFML, TGUI and CUDA. It renders Mandelbrot, Julia,
Newton, Nova, Septagon, Buddhabrot and anti-Buddhabrot with threads on every CPU core, and
uses an NVIDIA GPU through CUDA when there is one.

Ported from [pmiloslavsky/demo/fractals](https://github.com/pmiloslavsky/demo/tree/master/fractals)
(source commit `4e55d14`) into a single self-contained executable for Windows and Linux. The
plan and milestones are in [GOALS.md](GOALS.md).

## Download and run

Get the latest release from the
[Releases page](https://github.com/pmiloslavsky/aidemo/releases?q=fractals): one file,
nothing to install.

**Windows 10/11 (x64):** download `fractals.exe` and double-click it.

- The exe isn't code-signed yet, so the first time Windows SmartScreen says *"Windows
  protected your PC"*. Click **More info**, then **Run anyway**.
- The Visual C++ runtime and the CUDA runtime are built in; no redistributables are needed.

**Linux (x86-64):** download `fractals`, then:

```bash
chmod +x fractals
./fractals
```

- Built on Ubuntu 22.04; it runs on 22.04 and newer (24.04 and 26.04 checked) and other
  distributions with glibc 2.34 or newer.
- It needs an X11 or XWayland desktop with OpenGL, which every desktop install has. It
  also works in WSL on Windows 11 (WSLg).

**GPU:** with an NVIDIA GPU (GTX 10xx or newer) and its driver installed, Mandelbrot, Julia
and Buddhabrot render on the GPU. Without one, everything runs on CPU threads. The log
says which (`FractalsData/fractals.log`).

**Files:** the app keeps its files in a `FractalsData` folder next to the executable:
saved keys, screenshots, themes, images for image coloring, and the log. If that folder
isn't writable, it uses `%LOCALAPPDATA%\Fractals` or `~/.local/share/fractals` instead.

## Controls

| Input | Action |
|---|---|
| **Fractal** menu (bottom left) | choose the fractal |
| Mouse wheel | zoom (down: in, up: out), one step per notch |
| Right click | recenter on that point |
| Left drag | zoom to the selected rectangle |
| `z` | undo the last zoom or pan |
| `c` | GPU (CUDA) on/off for this fractal; the status line shows the state |
| `s` | screenshot to `FractalsData/screenshots/` |
| `n` | next image for the USE_IMAGE coloring |
| `g` / `h` | hide or show the GUI / the fractal |
| `p` | pause or resume rendering |
| `f` | exclusive fullscreen |
| `e` | exit |

The panel below the menu sets iterations, power, the Julia constant and the escape radius,
and chooses palettes and coloring for the outside and the inside of the set. **Save Key**
stores the current view as a JSON key; **Load Next Key** steps through the saved keys.

## Command line

`fractals --help` lists the options, the data folders and the JSON key format.

| Option | |
|---|---|
| `--windowed` | a window (3/4 of the screen) instead of borderless full screen |
| `--console` | show the log in a console (Windows; Linux always prints it) |
| `--cuda` | start with the GPU on for every fractal that supports it |
| `--no-cuda` | never use the GPU |
| `--threads <n>` | number of render threads (default: CPU threads − 1, at most 32) |
| `--save-and-exit <key.json> <out.png>` | render a key at 2560x1440, save it as a PNG and exit |
| `--hide` | with `--save-and-exit`: keep the window hidden |
| `-h`, `--help` | show the help |

For example, to render a saved key without showing a window:

```bash
fractals --save-and-exit FractalsData/keys/my_key.json my_key.png --hide
```

`--save-and-exit` also writes the key it rendered to `changed_key.json` in the current
folder. `tools/make_fractal_movies.py` uses it to turn a key into an animated GIF and
MP4s (zooming in while the light moves).

## Building from source

The build pulls SFML, TGUI, nlohmann/json and CMakeRC at the versions pinned in
`cmake/versions.cmake`. CUDA is optional: without `nvcc` the build is CPU-only.

**Windows:** Visual Studio 2022 with the C++ workload (its bundled CMake and Ninja work),
and optionally the CUDA Toolkit 12.x.

```powershell
.\scripts\setup-windows.ps1
.\scripts\build-windows.ps1
```

**Linux:** `scripts/setup-linux.sh` installs the compiler, CMake, Ninja and the X11/GL
headers with apt (it asks for sudo). CUDA is used if it's already installed.

```bash
scripts/setup-linux.sh
scripts/build-linux.sh
```

The executable is in `build/<preset>/bin/`. Presets: `windows-release`, `linux-release`,
their `-debug` versions and `-cpu-only` versions. `scripts/smoke-test.{sh,ps1}` renders
the keys in `tests/smoke/` as a quick check. GitHub Actions builds both on every push, and a
`fractals-v*` tag publishes a release.

## Layout

| Path | Contents |
|---|---|
| `src/` | Application and CUDA kernel sources |
| `assets/themes/`, `assets/escape_images/` | Default themes and coloring images, embedded in the executable |
| `scripts/` | Setup, build and smoke-test scripts (used by CI too) |
| `tests/` | Smoke-test and benchmark keys |
| `tools/` | Movie script, third-party notices generator |

## Performance

Measured at 2560x1440 on an AMD Ryzen 9 9900X (12 cores, 23 render threads) and an
NVIDIA RTX 5070, Windows 11, release build. "Frame" is the time the slowest render thread
takes for its slice of the picture, from `--save-and-exit` (see the log). The keys are in
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

## License notices

The executable includes SFML, TGUI, FreeType, HarfBuzz, SheenBidi, stb_image, nlohmann/json,
CMakeRC, tinycolormap and the NVIDIA CUDA runtime. Their licenses are in
[THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt), which ships with each release.
