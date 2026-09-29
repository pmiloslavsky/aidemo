# Fractals: goals

Port of https://github.com/pmiloslavsky/demo/tree/master/fractals into
https://github.com/pmiloslavsky/aidemo under `fractals/`.

## Distribution

The user downloads one file and runs it. Nothing else needs to be installed.

- **Windows:** one `fractals.exe`.
  - SFML 3 and TGUI 1.x linked statically.
  - MSVC runtime linked statically (`/MT`), so no VC++ Redistributable is needed.
  - The CUDA runtime is linked statically (`cudart_static`). Kernels are built as a
    fatbin for several GPU generations plus PTX.
- **Linux:** one `fractals` ELF binary.
  - SFML 3, TGUI 1.x and cudart linked statically.
  - Built on an older distro (Ubuntu 22.04) so it runs on newer ones.
  - Relies only on system glibc, X11, libGL and libudev.
  - An AppImage is optional, added later only if users hit missing-library problems.
- **CUDA is optional at runtime.** The driver (`nvcuda.dll` / `libcuda.so`) is never
  linked directly (no `-lcuda`). With no NVIDIA driver present, the app falls back to
  CPU threads instead of failing to start.
- **Build:** one CMake project replaces the Makefile and the vcxproj.
  - SFML and TGUI are pulled at pinned versions with `FetchContent`.
  - CUDA is optional at build time (CPU-only build if no toolkit).
- **CI:** GitHub Actions builds the Windows and Linux binaries on every push and uploads
  them as artifacts. Pushing a `fractals-v*` tag (for example `fractals-v1.0`) also
  attaches them to a GitHub Release.
- **Unsigned for now.** The README explains the SmartScreen "More info → Run anyway"
  step on first launch. Code signing can come later.

## Shared build environment (local = CI)

A local build and a CI build run the same commands with the same versions.

- **Pinned versions in one place:** `cmake/versions.cmake` holds the SFML, TGUI,
  nlohmann/json and CMakeRC git tags, plus the CUDA architectures list.
  - **SFML 3.1.0** (April 2026) and **TGUI 1.13.0** (June 2026), the newest stable releases.
    Neither project has an LTS line. The old build used SFML 3.0.0 and TGUI 1.10.0.
- **`CMakePresets.json`:** named configure and build presets (`windows-release`,
  `linux-release`, plus `-debug` and `-cpu-only` variants). Locally and in CI the build is
  `cmake --preset X`, then `cmake --build --preset X`.
- **No Docker.** Each OS has one setup script and one build script, used by both CI and
  local builds.
  - Linux: `scripts/setup-linux.sh` apt-installs the compiler, CMake, Ninja and the
    X11/udev/GL dev packages. CI runs it on `ubuntu-22.04`.
  - Windows: `scripts/setup-windows.ps1` only **checks** for VS 2022, CMake and Ninja (the
    ones bundled with VS count) and reports what's missing. It installs nothing unless
    asked. CI runs it on `windows-2022`, which already has these tools.
  - `scripts/build-linux.sh` and `scripts/build-windows.ps1` run the preset. The Windows
    script loads the MSVC environment (`vcvars64`) first.
- **The NVIDIA toolkit is never touched on local machines.**
  - The setup scripts install CUDA only when asked (`--install-cuda` / `-InstallCuda`).
    Only CI passes that flag, to get the pinned version on a clean runner.
  - Locally, the scripts skip CUDA setup by default, so an existing CUDA install is left
    alone.
  - The build uses whatever `nvcc` is already installed (any CUDA ≥ 12.0; CMake finds
    it). If none is found, it builds CPU-only and says so.
  - `-DFRACTALS_CUDA=OFF` (the `-cpu-only` presets) forces a CPU-only build even when CUDA
    is installed.
- The CI workflow only calls these scripts and presets; it has no build logic of its own.
- **CUDA version:** CI pins **CUDA 12.9** to match the dev machine. The architecture list
  must include `sm_120`, because the dev GPU is an RTX 5070 (Blackwell), which needs
  CUDA ≥ 12.8.

## Development workflow (this machine)

Uses only what's already installed: nothing new goes on the system and NVIDIA is never
touched.

- **Toolchain:**
  - VS 2022 Community (MSVC 14.44), plus the CMake and Ninja bundled with VS.
  - CUDA 12.9, already installed; driver 610.88 on an RTX 5070.
- **Dependencies:** SFML, TGUI and the other libraries download as source into the build
  folder through `FetchContent`, not into the system.
- **Testing:** build, run the exe, and verify rendering by having the app save PNGs in
  `--save-and-exit` mode. Covers both the CUDA and CPU paths.
- **Linux:** the release binaries are built only by CI on `ubuntu-22.04`. Optionally, a
  CPU-only smoke build can run in the existing WSL Ubuntu-26.04. That only apt-installs
  inside WSL, never CUDA, and nothing on Windows.

## Runtime files: a data folder next to the executable

Everything the app reads or writes lives in one folder that the app creates:

```
fractals.exe
FractalsData/
  themes/          default themes, extracted from the exe on first run; user edits are kept
  keys/            saved fractal keys (*.json)
  screenshots/     screenshot hotkey output
  escape_images/   all images from the old repo, extracted on first run; users can add their own
  fractals.log     log output (the Windows build has no console window)
```

- Paths are resolved from the **executable's location**, not the current working directory.
  This replaces the hardcoded `..\..\..\themes`, `..\..\..\screenshots` and similar paths.
  - Windows: `GetModuleFileNameW`.
  - Linux: `/proc/self/exe`. In an AppImage, use the directory of `$APPIMAGE` instead.
- Default files (themes, fonts, all escape images, about 5 MB) are built into the exe with
  [CMakeRC](https://github.com/vector-of-bool/cmrc), which works with MSVC, GCC and Clang.
- On startup, `std::filesystem::create_directories` creates any missing folders.
  - Embedded default files are written out only if they are missing, so user changes survive.
- If the exe's folder isn't writable (Program Files, `/usr/bin`, a read-only mount), fall
  back to the per-user data dir.
  - Windows: `%LOCALAPPDATA%\Fractals`.
  - Linux: `$XDG_DATA_HOME/fractals`, or `~/.local/share/fractals` if that isn't set.
- The command-line `--save-and-exit` mode (used by `make_fractal_movies.py`; it was
  `save_and_exit <key> <png> [hide]` until M7) keeps working
  with the same folder layout.

## Fractal keys: JSON

- Keys are saved as human-readable JSON (`FractalsData/keys/<name>.json`), not a raw memory
  dump of `SavedFractal`.
- Every key has a `"format_version"` field. Fields are named, so their order doesn't matter.
  A missing field uses its default, and an unknown field is ignored, so adding fields never
  breaks old keys.
- Enums are stored by **name**, not number (for example `"palette": "UF16"`,
  `"color_algo": "SHADOW_MAP"`). Numbers are fragile: adding Turbo and Cubehelix to
  tinycolormap moved UF16 from 11 to 13.
- Only the fractal description is saved: fractal type, iterations, power, zconst,
  escape radius, view, coloring and lighting. UI and runtime state (`show_selection`,
  `image_loaded`, `escape_image_w/h`) is not saved.
- Doubles are written with full round-trip precision (`%.17g`) so deep-zoom coordinates
  aren't lost.
- JSON library: nlohmann/json, fetched with `FetchContent`.
- `make_fractal_movies.py` reads and writes the JSON keys with the standard `json` module
  instead of the `ctypes.Structure` mirror.
- No import of old binary keys. The only existing key (`x.fractal_key_version_1`, saved
  with an older struct layout) is not carried over to aidemo.

## GPU (CUDA)

Today only Buddhabrot/Nebulabrot uses the GPU. Every other fractal runs on CPU threads.

**Build (M1):**
- CMake's native CUDA support replaces the VS CUDA build customization.
- The runtime is linked statically (`CUDA_RUNTIME_LIBRARY Static`), matching the `/MT` C++
  runtime.
- GPU code: `CMAKE_CUDA_ARCHITECTURES = 61;75;86;89;120-real;120-virtual`.
  - That covers GTX 10xx, RTX 20xx, RTX 30xx, RTX 40xx and RTX 50xx.
  - PTX for `compute_120` lets future GPUs compile the kernels on first launch (JIT).
  - The old vcxproj set no architecture, so it relied on the default `sm_52` plus JIT.

**Runtime detection and fallback (M5):**
- Check the result of `cudaGetDeviceCount` (it's currently ignored).
- Log the GPU name, the driver version (`cudaDriverGetVersion`) and the runtime version.
- A missing driver, a driver older than CUDA 12.9 needs, or no NVIDIA GPU → log it and use
  CPU threads.
- `checkCUDAError` currently calls `exit()` on any error, which kills the whole app. It
  becomes: return the error → turn CUDA off for the session → continue on CPU.
- Windows TDR: the display driver resets any GPU call that runs longer than ~2 s, which
  shows up as an error. Keep each kernel launch well under that (split the samples into
  chunks if needed).

**Known risks to check in M1/M5:**
- `cuDoubleComplex trail[10000]` is 160 KB of local memory per GPU thread. The driver
  reserves that for every thread that can run at once (gigabytes), so the launch may fail
  with out-of-memory on smaller cards. The fix is to shrink the trail or not store it.
  Also check that max iterations can't exceed 10000 (buffer overrun).
- Every call allocates, copies and frees three full-screen hit buffers (about 90 MB at
  1440p) in both directions. Keeping them on the GPU between calls is a cheap speedup.
- Consumer GPUs run double precision at 1/64 of single-precision speed. That largely
  explains why 16 CPU cores beat the GPU. Keeping this as-is is fine for the port.

**Testing:**
- Locally on the RTX 5070: the CUDA and CPU Buddhabrot images should match
  *statistically* (it's random sampling, so not pixel-exact).
- `CUDA_VISIBLE_DEVICES=-1` forces the CPU path.
- CI has no GPU. It compiles the kernels, and its smoke render exercises the real
  "no driver" fallback.

**Mandelbrot on the GPU (M6):**
- Today the `C` key sets `cuda_mode` and the label says "Cuda Running" for every fractal,
  but only Buddhabrot reads the flag. Mandelbrot always renders with `getImagePixels` on
  the CPU. M6 adds a real kernel for it (see the M6 milestone).

**Later (M10+ candidates):**
- Newton, Nova and Septagon on the GPU (Julia is covered in M6).
- Use float or double-float math where deep-zoom precision allows.

## Port scope (what carries over)

- Port `fractals_with_gui_cuda.cpp` + `buddha_cuda_kernel.cu`, keeping the single-file
  structure until M9.
- **Copied from the old repo:** all `escape_image/*.jpg`, the themes, `tinycolormap.hpp`,
  and `make_fractal_movies.py` (updated in M4).
- **Not copied:** `fractals_with_gui.cpp` (CPU-only legacy), `screenshots/` (109 MB),
  `fractal_movies/` (127 MB), `fractal*.png`, `swirl_fractal.py`, the vcxproj, the
  Makefile, the old key file.
- **Repo:** `aidemo` is cloned into `C:\Users\phili\claude_fractal`, and this file moves to
  `fractals/GOALS.md`. Claude commits and pushes freely (approved by the user).

## Milestones

Each milestone ends with a working app and a local commit. "Verify" is what has to pass
before moving on.

### M0: Repo skeleton ✅
- Layout: `src/` (the unchanged sources), `assets/themes/`, `assets/escape_images/` (all 10
  images), `tools/make_fractal_movies.py`. Root `.gitignore` and `.gitattributes`.
- Copied from demo commit `4e55d14`.
- Clone `aidemo`, create `fractals/`, and copy in the sources and assets listed above.
- Move this file into the repo. Add `.gitignore` (build folders, `FractalsData/`).
- **Verify:** the tree matches the port scope; nothing large or unwanted is committed.

### M1: Windows CMake build, static, same behavior ✅
- Done: `fractals.exe` is 10 MB. `dumpbin /dependents` shows only OPENGL32, WINMM, GDI32,
  KERNEL32, USER32 and ADVAPI32 (no MSVC runtime, no nvcuda.dll). It renders Mandelbrot
  the same as the old build and detects the RTX 5070.
- Paths are temporarily relative to the working directory (assets staged next to the exe)
  until M3.
- Visible TGUI 1.13 difference: the interior-coloring list box now shows a scrollbar.
- Interactive runs with `--windowed` (see M3) open a 3/4-size window at the left edge of
  the screen, so the rest of the desktop stays visible. The fractal still renders at full
  size and is scaled down; mouse coordinates are mapped back.
- GUI testing uses computer use. A per-user Start-menu shortcut "Fractals" makes the dev
  exe grantable. Click the canvas before a key so the window has focus. (Buddhabrot
  starts with CUDA on, so the first `c` turns it off; keys are not lost.)
- CUDA Buddhabrot checked interactively on the RTX 5070: `c` switches to "Cuda Running"
  and the image builds with the same structure as the CPU render.
- Python 3.14 (via the Python install manager) is installed on the dev machine for the
  tools scripts.

- `CMakeLists.txt`, `cmake/versions.cmake`, `CMakePresets.json`,
  `scripts/setup-windows.ps1` (check only) and `scripts/build-windows.ps1`.
- FetchContent SFML 3.1.0 + TGUI 1.13.0, both static; `/MT`; `cudart_static`, no `-lcuda`;
  fatbin including `sm_120`.
- Port the code to SFML 3.1 / TGUI 1.13 (fix deprecations).
- Themes and escape image still loaded from disk, as before (M2 changes that).
- **Verify:** builds with the tools already installed. `dumpbin /dependents` shows only
  system DLLs. The app runs on the RTX 5070, and a `--save-and-exit` render matches the old
  build.

### M2: CI early (Windows + Linux build only) ✅
- Done: both CI jobs are green (run 36346384540). Windows installs CUDA 12.9 and builds
  in about 4.5 min; Linux about 1 min with a warm cache. Artifacts: 8 MB (Windows) and
  10 MB (Linux, 15 MB unzipped), zipped. The CI Linux binary needs at most GLIBC_2.34 and
  loads on Ubuntu 26.04 (WSL). SFML loads libGL at runtime.
- Follow-up (done 2026-09-29): actions bumped to Node 24 versions (checkout v7, cache v6,
  upload-artifact v7, download-artifact v8). GitHub warned that the v4 actions ran on the
  deprecated Node 20; bump them when Node 24 versions are out.
- Local status: `linux-release-cpu-only` builds in WSL Ubuntu 26.04 (GCC 15) with no
  source changes (the M1 port already fixed the GCC issues). `readelf -d` shows only
  X11, Xrandr, Xcursor, Xi, udev, libm, libc and the loader. `scripts/check-linux-deps.sh`
  fails the build if anything else shows up.
- Linux fixes: `SFML_USE_SYSTEM_DEPS OFF` (SFML defaults to the system FreeType on
  Linux) and a FreeType → HarfBuzz link so GNU ld resolves their circular dependency.
  `-static-libstdc++ -static-libgcc`; the static-std-lib options are Windows-only.
- CI CUDA: `FRACTALS_CUDA_CI_VERSION` is now the full version (12.9.1), which the Windows
  network installer URL needs. Windows installs `nvcc cudart curand_dev`
  (`scripts/install-cuda-ci.ps1`); Linux installs `cuda-nvcc`, `cuda-cudart-dev` and
  `libcurand-dev` from NVIDIA's apt repo.
- Build cache: the whole `build/<preset>` folder, keyed by the versions/CMake files plus
  the commit, restored from the newest matching entry.

- `scripts/setup-linux.sh`, `scripts/build-linux.sh`, Linux presets.
- `.github/workflows/fractals.yml`: `windows-2022` + `ubuntu-22.04`, CUDA 12.9 installed
  in CI only, build caching, artifacts uploaded.
- Fix whatever breaks on GCC/Linux (the `#ifdef _WINDOWS` paths, the missing `;` after
  `keys_location`, and so on).
- **Verify:** both jobs are green; `ldd` on the Linux binary shows only system libraries.
  The first push needs your approval.

### M3: Self-contained runtime ✅
- Done. `src/runtime.{h,cpp}` picks the data folder: `FractalsData` next to the exe
  (`GetModuleFileNameW` / `/proc/self/exe` / `$APPIMAGE`), or the per-user folder when the
  exe's folder isn't writable (checked by writing a probe file). CMakeRC 2.0.1 embeds
  `assets/themes` and `assets/escape_images`; files already on disk are never overwritten.
  The themes use TGUI's built-in font, so there is no font file to embed.
- Keys (still the old binary format until M4) go to `FractalsData/keys/fractal_key_version_1/`.
  `--save-and-exit` still takes key and PNG paths relative to the working directory.
- Window: borderless at the desktop resolution by default; `--windowed` opens the
  3/4-size window at the left edge (the Start-menu test shortcut passes it). `--save-and-exit`
  always renders at 2560x1440 so frames are reproducible.
- Windows builds as a GUI app (`WIN32_EXECUTABLE`, `/ENTRY:mainCRTStartup`), so there is no
  console. `std::cout`/`std::cerr` go to `fractals.log`, and also to the console on Linux and
  with `--console` on Windows. The CUDA host code's `printf`s became `std::cout` so they
  reach the log. The one device-side `printf` (max iterations) is left for M5.
- CMake 4 rejects CMakeRC's `cmake_minimum_required(3.3)`, so `CMAKE_POLICY_VERSION_MINIMUM`
  is set to 3.5 around its include.
- Also fixed: `--save-and-exit` read `argv[4]` when only 4 args were passed.
- Verified: the lone exe in an empty folder creates `FractalsData/` with all 15 files and
  a log with the GPU info; a user-edited theme file survives a restart; with the folder
  made unwritable (icacls deny) it uses `%LOCALAPPDATA%\Fractals`; the Linux build does
  the same from `/proc/self/exe` (WSLg). Interactive (computer use): the theme loads, `N`
  cycles escape images including the PNG, `S` writes to `FractalsData/screenshots/`.
- `--console` verified from PowerShell. PowerShell doesn't wait for a GUI-subsystem exe, so the
  prompt returns first and the output prints over it; `.\fractals.exe --console | Out-Host`
  waits. CI run 36347544190 is green; the artifacts are now just the binary.
- The CI Linux build (with CUDA) runs in WSL Ubuntu 26.04 and uses the RTX 5070 through
  the WSL driver (`/usr/lib/wsl/lib/libcuda.so`); the user confirmed CUDA works there.
  WSLg once showed no Linux windows at all (only a "[WARN:COPY MODE]" stub); even a plain
  X11 window didn't appear. That was WSLg itself, and restarting WSL fixed it.
- SFML opens the X11 display during static initialization, so on Linux the app aborts
  before `main` without a display. The M5 CI smoke test needs `xvfb`.
- `FractalsData/` next to the exe, resolved from the exe path, with the per-user fallback
  when the folder isn't writable.
- CMakeRC embeds the themes, font and all escape images; missing files are extracted on
  startup.
- Borderless fullscreen at native resolution; no console on Windows; `fractals.log`;
  `--console` flag.
- Fix the escape-image PNG fallback.
- **Verify:** copy only the exe to an empty folder, run it, and check that `FractalsData/`
  is created and everything works. Check the fallback from a read-only folder.

### M4: JSON keys + movie script ✅
- Done. Keys are `FractalsData/keys/<fractal>_<crc>.json`; "Load Next Key" goes through
  `*.json` in name order. Format (every field optional except `fractal`):
  `format_version`, `fractal` (by name), `max_iterations` [3], `power`, `zconst` [re, im],
  `escape_radius`, `random_sample`, `view` {`x_start`, `y_start`, `zoom`,
  `requested_zoom`, `theta`}, `coloring` {`algo`, `cycle_size`, `palette`,
  `reflect_palette`}, `lighting` {`pos_r`, `pos_i`, `angle`, `height`}, `interior` {same
  as coloring}. Interior coloring is new: the old binary keys didn't store it.
- The view is stored without the per-pixel values (`xdelta`, `current_width`, ...); they
  are recalculated for the current window when a key loads, so a key saved at one
  resolution loads at another. Loading zooms about the center from `zoom` to
  `requested_zoom`, which is what the movie script animates.
- Doubles: nlohmann writes the shortest text that parses back to the same double, which
  is lossless like `%.17g` (checked at 1e-12 zoom).
- Unknown enum names fall back to the default (the first entry of each name table);
  an unknown fractal name makes the key fail to load, with a log message.
- One capture/apply helper replaces the four copies of the load code. Also fixed:
  the palette list showed Hot and Turbo swapped (list order didn't match the enum), and
  the in-memory "Save Fractal" list could write one past its end.
- `--save-and-exit` writes `changed_key.json` (was `changed_key.fractal_key_version_1`).
- `tools/make_fractal_movies.py`: JSON keys; finds the exe in `build/*-release*/bin`
  (or `--exe`); `--key`, `--out`, `--frames-only`, `--keep-frames`; no `shell=True`, so it
  runs on Linux; frames sorted by number. Pillow and moviepy are needed only for the
  GIF/MP4 step.
- Verified: a hand-written key renders, and rendering the `changed_key.json` it writes
  gives a byte-identical PNG and an identical key. GUI (computer use): "Load Next Key"
  loads a key and "Save Key" writes one with the same view and coloring. The movie script
  made 4 frames, a GIF and 2 MP4s (Pillow/moviepy in a scratch venv).
- Windows vs Linux (WSL, GCC 15) render of the same key: 1,979 of 3.7M pixels differ
  (0.05%), all near the chaotic boundary, where MSVC's and glibc's `pow`/`log` round
  differently and an escape count changes by one. Same tolerance as the M6 GPU/CPU rule.
- nlohmann/json save/load, enums by name, `%.17g` doubles, defaults for missing fields.
- `make_fractal_movies.py` switched to JSON.
- **Verify:** save → load round trip reproduces the same image; a key saved on Windows
  renders the same on Linux (CI); the movie script generates frames.

### M5: CUDA-optional hardening ✅
- Done: CI run 36351081240 is green. On both runners (no GPU) the smoke test logs
  "CUDA: no NVIDIA driver, using CPU threads" and renders the key (Linux under xvfb,
  Windows with Mesa llvmpipe).
- `cuda_init()` logs the runtime and driver versions and the GPU, and returns
  false, logging why, for no driver, no GPU (`CUDA_VISIBLE_DEVICES=-1` checked), a driver
  too old for CUDA 12, or no kernel for the GPU (checked with `cudaFuncGetAttributes`).
  CUDA errors are logged and returned instead of calling `exit()`. The render thread then
  turns CUDA off for the session and restores the CPU hit buffers. Detection now also runs
  in `--save-and-exit`, which only matters for Buddhabrot until M6. The status line shows
  "Cuda N/A" when there is no usable GPU.
- Kernel: the `trail[10000]` array (160 KB of local memory per thread) and its 10000
  iteration cap are gone. Each orbit is iterated twice instead (test, then plot); only
  escaping orbits, which are short, pay for the second pass. Hit and stats buffers stay on
  the GPU and are zeroed each call. The stats buffer was never zeroed before, so the GPU
  stats started from garbage. The grid is 4 blocks per SM (was 32 blocks for 48 SMs): about
  195k samples/s against 84k before on the RTX 5070.
- Watchdog: the samples per thread adapt so each launch takes about 200 ms (measured
  100–300 ms). All blocks run in one wave, so a launch lasts about one thread's worst-case
  orbits. One sample at red=10000 takes about 100 ms, so the ~2 s limit is only at risk
  above roughly 150k iterations; the log warns when a one-sample launch exceeds 1 s.
- Removed the unused test and prototype kernels (`cuda_vec_add`, `cudaTest`, ...).
- Smoke test: `tests/smoke/mandelbrot.json` plus `scripts/smoke-test.{sh,ps1}`. The binary
  is copied into an empty folder and renders the key; the test checks the PNG,
  `changed_key.json`, the log's CUDA line and the extracted assets. It passes locally on
  Windows with and without `CUDA_VISIBLE_DEVICES=-1`, and on Linux (WSL, CPU-only build).
  Only a Mandelbrot key: in `--save-and-exit`, probabilistic fractals never count as done.
- CI Windows has no GPU, so only Windows' OpenGL 1.1 (textures up to 1024x1024): the 2560x1440
  texture failed and saving the screenshot threw an uncaught exception (exit 0xC0000409).
  The app now logs the OpenGL version and texture limit at startup, reports a texture
  failure once, and a failed screenshot logs instead of crashing. CI puts Mesa's llvmpipe
  `opengl32.dll` next to the exe copy (`smoke-test.ps1 -SoftwareGL`; version pinned in
  `versions.cmake`; unpacked with 7-Zip on the runner, whose `tar` has no LZMA).
- `-h`/`--help` prints the usage, the folders and the JSON key format (on Windows it
  attaches to the calling console). An unknown option prints the usage and exits with 2;
  it used to be taken as the thread count.
- Not tested: a real CUDA failure in the middle of a session.
- A missing driver, no NVIDIA GPU or a CUDA error falls back to CPU threads with a log
  message instead of crashing.
- CI smoke test: the no-GPU runners run `--save-and-exit` headless (Linux under `xvfb`)
  and check that a PNG comes out. That exercises the no-driver path for real.
- **Verify:** CI smoke tests pass on both OSes; locally, `CUDA_VISIBLE_DEVICES=-1`
  forces the CPU path.

### M6: CUDA Mandelbrot (+ Julia) ✅
- Done (CI run 36353690222 green; the runners take the CPU path). `cuda_escape_time` runs one GPU thread per pixel for a render thread's
  columns, in column tiles sized from the measured speed (worst case: every pixel to max
  iterations; target 400 ms per tile). The CPU colors the results with
  `color_escape_pixel`, the same code as the CPU path. Per-thread CUDA streams;
  `cudaDeviceScheduleBlockingSync` so waiting threads don't spin.
- `escape_pow`: integer powers 1–16 use plain complex multiplication on both CPU and GPU
  (exact IEEE, FMA off in nvcc with `--fmad=false`); other powers use `std::pow`'s formula.
  With `pow` for z², GPU and CPU differed on 0.01% (full view) up to 63% (zoom 1e-13) of
  the pixels because CUDA's and MSVC's `log`/`exp` round differently. Now the 7 keys in
  `tests/benchmark/` match pixel for pixel on CPU and GPU, from the full view to 1e-13.
  This changes CPU images slightly (in the last bits; at deep zoom they become correct)
  and makes the CPU 3.6–8.5x faster.
- The GPU is 1.8–3.5x faster than 23 CPU threads (table in the README), so
  Mandelbrot_300, Mandelbrot_1000 and Julia now default to `cuda_mode` on. It only takes
  effect with a working GPU.
- Found while profiling: the UF16 palette was built with 17 heap allocations per pixel
  (heap-lock contention across threads; a GPU frame took ~1 s). Now a constant table.
- `--cuda` / `--no-cuda` options. `--save-and-exit` logs the frame time (the slowest
  thread's slice). Status line: "Cuda Running" only after the GPU has rendered, "Cuda N/A"
  when there's no GPU or no kernel for the fractal's current settings.
- `has_gpu_kernel`: the Buddhabrot kernel only does z^2 + c, so the Julia, anti and
  other-power Buddhabrots now always use the CPU. Before, `c` ran the plain kernel for them
  and drew the wrong fractal.
- Interactive check (computer use): Mandelbrot starts "Cuda Running" (~59M samples/s vs
  8M on the CPU), `c` toggles it, zooming re-renders on the GPU.
- A new escape-time kernel with one GPU thread per pixel. It runs the same iteration as the
  CPU `getImagePixels` path: the configurable power, the escape radius, and the max
  iterations.
  - It writes the per-pixel results the CPU coloring already uses (iteration count, smooth
    value, and whatever the shadow-map and interior coloring need) into a device buffer
    that stays on the GPU between frames.
  - The existing CPU coloring turns them into pixels, so the look doesn't change.
- Julia uses the same loop with `z0 = pixel` and `c = zconst`, so it comes almost free.
- When `cuda_mode` is on and a GPU is present, the render thread calls the kernel instead
  of `getImagePixels`. Any CUDA error falls back to CPU (M5).
- The "Cuda Running" label is shown only when the kernel actually rendered the frame.
  Fractals without a kernel show "Cuda N/A".
- Kernel launches are split into tiles so each stays well under the Windows ~2 s GPU
  timeout at high max-iterations.
- **Verify:**
  - GPU and CPU renders of the same JSON keys match pixel for pixel, or within 1
    iteration at boundary pixels where rounding differs. Check at several zoom depths down
    to ~1e-13, the double-precision limit.
  - Log frame times for CPU vs GPU at 1440p for a few keys, and record them in the README.
  - The CI smoke render (no GPU) still takes the CPU path.

### M7: Releases + docs ✅
- Done: tag `fractals-v0.9-test` made the pre-release
  https://github.com/pmiloslavsky/aidemo/releases/tag/fractals-v0.9-test (kept, per the user).
  Downloaded: checksums OK; `fractals.exe` (Windows) and `fractals` (WSL Ubuntu 26.04) each
  run from an empty folder, create `FractalsData/`, render on the RTX 5070 and give
  byte-identical PNGs. (That release predates the `--save-and-exit` option rename.)
- `.github/workflows/fractals-release.yml`: a `fractals-v*` tag runs `fractals.yml` (now
  also `workflow_call`), then creates the Release with `fractals.exe`, `fractals`,
  `THIRD_PARTY_NOTICES.txt` and `SHA256SUMS.txt`; the body is `RELEASE_NOTES.md`. A `-` after
  the version (`fractals-v0.9-test`) makes a pre-release.
- `THIRD_PARTY_NOTICES.txt` is generated from the downloaded sources' license files by
  `tools/make_third_party_notices.py` (rerun after changing `versions.cmake`). It covers
  SFML, TGUI, FreeType, HarfBuzz, SheenBidi, stb_image, qoi, cpp-unicodelib, glad,
  nlohmann/json, CMakeRC, tinycolormap and the CUDA runtime.
- Command line cleaned up: `--threads <n>`, `--save-and-exit <key> <png>` and `--hide`
  replace the positional `[threads]`, `save_and_exit ... [hide]`. Missing values or stray
  words print an error and the usage (exit 2). Threads are capped at 32 (`MAX_THREADS`, the
  per-thread arrays' size); CPUs with 34+ threads would have overrun them.
- Escape images: the user confirmed they are all public images; they stay embedded.
- A `fractals-v*` tag creates a GitHub Release with `fractals.exe` + the Linux
  `fractals`.
- README: download/run steps, SmartScreen note, controls, build instructions.
  `THIRD_PARTY_NOTICES` (SFML zlib, TGUI zlib, nlohmann MIT, CMakeRC MIT, tinycolormap MIT,
  CUDA runtime redistribution notice).
- **Verify:** a test tag produces a Release whose files download and run.

### M8: Clean-machine check → v1.0 ✅
- Done: the user chose to skip Windows Sandbox and the stock Ubuntu 22.04/24.04 checks
  (2026-09-27). Linux coverage is the CI build and smoke test on Ubuntu 22.04 plus the
  release binary on WSL Ubuntu 26.04. Tagged `fractals-v1.0`.
- Windows (user's machine, as the user asked instead of Windows Sandbox): the downloaded
  `fractals-v0.9-test` exe, alone in a new folder, created `FractalsData/`, opened borderless
  full screen by default and windowed with `--windowed`, ran Mandelbrot on the GPU (~62M
  samples/s, "Cuda Running"), and right-click recenter, wheel zoom (0.9 per step), `c` and
  `e` all worked. No redistributables are needed (`dumpbin /dependents`: system DLLs only).
- Follow-up: two instances in the same folder share `fractals.log` and clobber it.
- Computer-use quirk: with the borderless full-screen window in front, computer use
  thinks the desktop is frontmost and blocks input; `open_application` also starts a new
  instance without the shortcut's arguments. Test with `--windowed` via `Start-Process`.
- Run the Release exe on a Windows machine or VM with no VS, CUDA or redistributables
  (Windows Sandbox works for this), and the Linux binary on a stock Ubuntu 22.04 / 24.04 /
  26.04 (WSL is fine).
- Add an AppImage only if something is missing.
- **Verify:** it works everywhere → tag `fractals-v1.0`.

### M9: Code split (optional, before features) — needs the user's go-ahead
- Not approved yet. Discuss it with the user before starting any of it (2026-09-27).
- Split the 3,300-line file into modules: fractal math, CPU renderer, CUDA bridge, coloring,
  keys, GUI, app/paths. No behavior change.
- **Verify:** CI smoke renders are identical before and after.

### M10+: App changes
_TBD. To be discussed._

## Fixes after v1.0

- **Crash when pressing `n` quickly with USE_IMAGE coloring (interior or outside).**
  Reported 2026-09-29 with a WER dump (`%LOCALAPPDATA%\CrashDumps`). Access violation in
  `sf::Image::getPixel` (`fractals.exe+0x203138` in v1.0-era builds): the render threads
  read `NSR.escape_image` per pixel while the `n` handler replaced it on the main thread,
  freeing the old pixel buffer (and updating the width/height separately). Reproduced
  with the old exe (crash within 80 presses, same offset). Fix: `EscapeImage` is an
  immutable object behind a `shared_ptr`, swapped under a mutex; each render thread takes
  a snapshot per slice; pixel indexes are clamped. Survives 3 x 150 fast presses.
- `windows-relwithdebinfo` preset (optimized, with a PDB) for debugging crashes.
- **Audit for similar bugs (2026-09-29).** The GUI writes plain scalars (palette,
  iterations, light) that the render threads read without locks; at worst one frame mixes
  settings. Fixed:
  - Escape-time reset: one render thread `clear()`ed the whole shared `color` array
    while the others wrote to it (indexing past `size()`; it worked only because
    `clear()` keeps capacity). Removed; the next pass overwrites every pixel.
  - Buddhabrot reset: each thread `clear()`ed its hit arrays, which kept the old counts
    in memory, so new samples were added to the old view's hits. Now zero-filled.
  - NaN palette index: tinycolormap's `Clamp01` passes NaN, and `CalcLerp` turns it into a
    table index (undefined behavior; x64 gives index 0, so black). SMOOTH coloring makes NaN
    whenever an orbit escapes with |z| < 1: Newton/Nova with SMOOTH were 56% black, a small
    escape radius 28%. `palette_color()` maps NaN to 0; Newton SMOOTH is now fully colored.
