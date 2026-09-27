#!/usr/bin/env bash
# Installs the Linux build dependencies with apt (Ubuntu 22.04 or newer).
# CUDA is installed only with --install-cuda (CI). Never touches a GPU driver.
#   scripts/setup-linux.sh                 # compiler, CMake, Ninja, X11/udev/GL headers
#   scripts/setup-linux.sh --install-cuda  # + nvcc, cudart, curand headers (CI only)
set -euo pipefail

install_cuda=0
for arg in "$@"; do
  case "$arg" in
    --install-cuda) install_cuda=1 ;;
    *) echo "unknown option: $arg" >&2; exit 2 ;;
  esac
done

here="$(cd "$(dirname "$0")" && pwd)"
sudo=""
if [ "$(id -u)" -ne 0 ]; then sudo="sudo"; fi

$sudo apt-get update
$sudo apt-get install -y --no-install-recommends \
  build-essential git ca-certificates wget cmake ninja-build python3-pip \
  libx11-dev libxrandr-dev libxcursor-dev libxi-dev libudev-dev \
  libgl-dev libegl-dev

# CMakePresets.json needs CMake >= 3.26; Ubuntu 22.04 ships 3.22.
version_ge() { [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -n1)" = "$2" ]; }
cmake_version="$(cmake --version | head -n1 | awk '{print $3}')"
if ! version_ge "$cmake_version" 3.26; then
  echo "CMake $cmake_version is too old, installing a newer one with pip (user-local)"
  python3 -m pip install --user --upgrade cmake
  echo "Add ~/.local/bin to PATH if 'cmake --version' still shows $cmake_version"
  if [ -n "${GITHUB_PATH:-}" ]; then echo "$HOME/.local/bin" >> "$GITHUB_PATH"; fi
fi

if [ "$install_cuda" -eq 1 ]; then
  full="$(sed -n 's/^set(FRACTALS_CUDA_CI_VERSION \([0-9.]*\)).*/\1/p' "$here/../cmake/versions.cmake")"
  major_minor="$(echo "$full" | cut -d. -f1,2)"
  pkg_ver="$(echo "$major_minor" | tr . -)"
  . /etc/os-release
  distro="ubuntu$(echo "$VERSION_ID" | tr -d .)"
  echo "Installing CUDA $major_minor (nvcc + cudart + curand headers) from NVIDIA's $distro repo"
  tmp="$(mktemp -d)"
  wget -q -O "$tmp/cuda-keyring.deb" \
    "https://developer.download.nvidia.com/compute/cuda/repos/$distro/x86_64/cuda-keyring_1.1-1_all.deb"
  $sudo dpkg -i "$tmp/cuda-keyring.deb"
  $sudo apt-get update
  $sudo apt-get install -y --no-install-recommends \
    "cuda-nvcc-$pkg_ver" "cuda-cudart-dev-$pkg_ver" "libcurand-dev-$pkg_ver"
  cuda_bin="/usr/local/cuda-$major_minor/bin"
  if [ -n "${GITHUB_PATH:-}" ]; then echo "$cuda_bin" >> "$GITHUB_PATH"; fi
  "$cuda_bin/nvcc" --version | tail -n2
else
  echo "Skipping CUDA (pass --install-cuda to install it; CI only)"
fi
