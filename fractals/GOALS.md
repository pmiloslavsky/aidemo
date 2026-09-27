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
  `save_and_exit` mode. Covers both the CUDA and CPU paths.
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
- The command-line `save_and_exit` mode (used by `make_fractal_movies.py`) keeps working
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

**Later (M9+ candidates):**
- Run the escape-time fractals on the GPU (Mandelbrot, Julia, Newton, Nova, Septagon).
  Each pixel is independent, so these gain the most from a GPU.
- Use float or double-float math where deep-zoom precision allows.

## Port scope (what carries over)

- Port `fractals_with_gui_cuda.cpp` + `buddha_cuda_kernel.cu`, keeping the single-file
  structure until M8.
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

### M0: Repo skeleton
- Clone `aidemo`, create `fractals/`, and copy in the sources and assets listed above.
- Move this file into the repo. Add `.gitignore` (build folders, `FractalsData/`).
- **Verify:** the tree matches the port scope; nothing large or unwanted is committed.

### M1: Windows CMake build, static, same behavior
- `CMakeLists.txt`, `cmake/versions.cmake`, `CMakePresets.json`,
  `scripts/setup-windows.ps1` (check only) and `scripts/build-windows.ps1`.
- FetchContent SFML 3.1.0 + TGUI 1.13.0, both static; `/MT`; `cudart_static`, no `-lcuda`;
  fatbin including `sm_120`.
- Port the code to SFML 3.1 / TGUI 1.13 (fix deprecations).
- Themes and escape image still loaded from disk, as before (M2 changes that).
- **Verify:** builds with the tools already installed. `dumpbin /dependents` shows only
  system DLLs. The app runs on the RTX 5070, and a `save_and_exit` render matches the old
  build.

### M2: CI early (Windows + Linux build only)
- `scripts/setup-linux.sh`, `scripts/build-linux.sh`, Linux presets.
- `.github/workflows/fractals.yml`: `windows-2022` + `ubuntu-22.04`, CUDA 12.9 installed
  in CI only, build caching, artifacts uploaded.
- Fix whatever breaks on GCC/Linux (the `#ifdef _WINDOWS` paths, the missing `;` after
  `keys_location`, and so on).
- **Verify:** both jobs are green; `ldd` on the Linux binary shows only system libraries.
  The first push needs your approval.

### M3: Self-contained runtime
- `FractalsData/` next to the exe, resolved from the exe path, with the per-user fallback
  when the folder isn't writable.
- CMakeRC embeds the themes, font and all escape images; missing files are extracted on
  startup.
- Borderless fullscreen at native resolution; no console on Windows; `fractals.log`;
  `--console` flag.
- Fix the escape-image PNG fallback.
- **Verify:** copy only the exe to an empty folder, run it, and check that `FractalsData/`
  is created and everything works. Check the fallback from a read-only folder.

### M4: JSON keys + movie script
- nlohmann/json save/load, enums by name, `%.17g` doubles, defaults for missing fields.
- `make_fractal_movies.py` switched to JSON.
- **Verify:** save → load round trip reproduces the same image; a key saved on Windows
  renders the same on Linux (CI); the movie script generates frames.

### M5: CUDA-optional hardening
- A missing driver, no NVIDIA GPU or a CUDA error falls back to CPU threads with a log
  message instead of crashing.
- CI smoke test: the no-GPU runners run `save_and_exit` headless (Linux under `xvfb`)
  and check that a PNG comes out. That exercises the no-driver path for real.
- **Verify:** CI smoke tests pass on both OSes; locally, `CUDA_VISIBLE_DEVICES=-1`
  forces the CPU path.

### M6: Releases + docs
- A `fractals-v*` tag creates a GitHub Release with `fractals.exe` + the Linux
  `fractals`.
- README: download/run steps, SmartScreen note, controls, build instructions.
  `THIRD_PARTY_NOTICES` (SFML zlib, TGUI zlib, nlohmann MIT, CMakeRC MIT, tinycolormap MIT,
  CUDA runtime redistribution notice).
- **Verify:** a test tag produces a Release whose files download and run.

### M7: Clean-machine check → v1.0
- Run the Release exe on a Windows machine or VM with no VS, CUDA or redistributables
  (Windows Sandbox works for this), and the Linux binary on a stock Ubuntu 22.04 / 24.04 /
  26.04 (WSL is fine).
- Add an AppImage only if something is missing.
- **Verify:** it works everywhere → tag `fractals-v1.0`.

### M8: Code split (optional, before features)
- Split the 3,300-line file into modules: fractal math, CPU renderer, CUDA bridge, coloring,
  keys, GUI, app/paths. No behavior change.
- **Verify:** CI smoke renders are identical before and after.

### M9+: App changes
_TBD. To be discussed._
