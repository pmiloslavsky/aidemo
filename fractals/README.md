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
| `tools/` | `make_fractal_movies.py`, which renders key-framed movies with the app |

Build instructions arrive with milestone M1.

`src/tinycolormap.hpp` is © Yuki Koyama, MIT License.
