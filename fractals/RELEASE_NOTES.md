A fractal explorer: Mandelbrot, Julia, Newton, Nova, Septagon and Buddhabrot, on every
CPU core, and on an NVIDIA GPU when there is one. One file each, nothing to install.

**Windows 10/11 (x64):** download `fractals.exe` and run it. The exe isn't code-signed
yet, so Windows SmartScreen may say "Windows protected your PC": click **More info**,
then **Run anyway**.

**Linux (x86-64, glibc 2.34+, e.g. Ubuntu 22.04 and newer):** download `fractals`, then
`chmod +x fractals && ./fractals`. Needs an X11/XWayland desktop with OpenGL.

Run `fractals --help` for the options, the data folder and the JSON key format. Controls
and details: [README](https://github.com/pmiloslavsky/aidemo/tree/main/fractals#readme).
`SHA256SUMS.txt` has the checksums; `THIRD_PARTY_NOTICES.txt` the licenses of the
included libraries.
