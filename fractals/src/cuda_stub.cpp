// Used instead of buddha_cuda_kernel.cu when building without CUDA
// (-DFRACTALS_CUDA=OFF or no nvcc found). Reports no GPU so the app always
// takes the CPU thread path.
#include "buddha_cuda_kernel.h"

#include <iostream>

bool cuda_init(void) {
  std::cout << "CUDA: built without CUDA support, using CPU threads" << std::endl;
  return false;
}

int cuda_generate_buddhabrot_hits(
    unsigned int, unsigned int, SupportedFractal &, SampleStats &,
    std::vector<std::vector<long long unsigned int>> &,
    std::vector<std::vector<long long unsigned int>> &,
    std::vector<std::vector<long long unsigned int>> &) {
  return 1;
}

int cuda_escape_time(const EscapeParams &, unsigned int, unsigned int, unsigned int,
                     std::vector<EscapeResult> &, const bool *) {
  return 1;
}
