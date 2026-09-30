# Single source of truth for dependency versions and GPU targets.
# Local builds and CI both read this file.

set(FRACTALS_SFML_GIT_TAG 3.1.0)
set(FRACTALS_TGUI_GIT_TAG v1.13.0)
set(FRACTALS_CMRC_GIT_TAG 2.0.1)
set(FRACTALS_JSON_VERSION 3.12.0)
set(FRACTALS_BOOST_MP_GIT_TAG boost-1.92.0)  # Boost.Multiprecision, standalone

# CUDA toolkit CI installs, full version (local builds use whatever nvcc is installed, >= 12.0)
set(FRACTALS_CUDA_CI_VERSION 12.9.1)

# Mesa (llvmpipe software OpenGL) for the Windows CI smoke test only: the runners
# have no GPU, and Windows' built-in OpenGL 1.1 caps textures at 1024x1024.
# From https://github.com/pal1000/mesa-dist-win/releases
set(FRACTALS_CI_MESA_VERSION 26.2.3)

# GTX 10xx, RTX 20xx, 30xx, 40xx, 50xx (SASS) + PTX for GPUs newer than sm_120
set(FRACTALS_CUDA_ARCHITECTURES 61-real 75-real 86-real 89-real 120-real 120-virtual)
