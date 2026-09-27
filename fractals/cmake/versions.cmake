# Single source of truth for dependency versions and GPU targets.
# Local builds and CI both read this file.

set(FRACTALS_SFML_GIT_TAG 3.1.0)
set(FRACTALS_TGUI_GIT_TAG v1.13.0)
set(FRACTALS_CMRC_GIT_TAG 2.0.1)
set(FRACTALS_JSON_VERSION 3.12.0)

# CUDA toolkit CI installs, full version (local builds use whatever nvcc is installed, >= 12.0)
set(FRACTALS_CUDA_CI_VERSION 12.9.1)

# GTX 10xx, RTX 20xx, 30xx, 40xx, 50xx (SASS) + PTX for GPUs newer than sm_120
set(FRACTALS_CUDA_ARCHITECTURES 61-real 75-real 86-real 89-real 120-real 120-virtual)
