// Used instead of buddha_cuda_kernel.cu when building without CUDA
// (-DFRACTALS_CUDA=OFF or no nvcc found). Reports no devices so the
// app always takes the CPU thread path.
#include "buddha_cuda_kernel.h"

#include <iostream>

int cuda_info(void) {
  std::cout << "Built without CUDA support" << std::endl;
  return 0;
}

int cuda_vec_add(unsigned int, unsigned int) { return 0; }

int cuda_generate_hits_prototype(unsigned int, unsigned int) { return 0; }

int cuda_generate_buddhabrot_hits(
    unsigned int, unsigned int, SupportedFractal &, SampleStats &,
    std::vector<std::vector<long long unsigned int>> &,
    std::vector<std::vector<long long unsigned int>> &,
    std::vector<std::vector<long long unsigned int>> &) {
  return 0;
}
