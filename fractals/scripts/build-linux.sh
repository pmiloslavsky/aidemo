#!/usr/bin/env bash
# Configures and builds a CMake preset, then checks the binary's library deps.
#   scripts/build-linux.sh                          # linux-release
#   scripts/build-linux.sh linux-release-cpu-only
set -euo pipefail

preset="${1:-linux-release}"
cd "$(dirname "$0")/.."

# Use an installed CUDA toolkit even when it isn't on PATH.
if ! command -v nvcc >/dev/null 2>&1 && [ -x /usr/local/cuda/bin/nvcc ]; then
  export PATH="/usr/local/cuda/bin:$PATH"
fi

cmake --preset "$preset"
cmake --build --preset "$preset"

bin="$PWD/build/$preset/bin/fractals"
scripts/check-linux-deps.sh "$bin"
echo "Built: $bin"
