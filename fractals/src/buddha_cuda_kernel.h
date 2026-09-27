#pragma once
#include <vector>
#include "fractals.h"

// Looks for a usable NVIDIA GPU and logs what it finds (runtime and driver
// versions, GPU name). Returns false, after logging why, when there is no
// driver, no GPU, a driver too old for this build, or no kernel for the GPU;
// the app then renders with CPU threads.
bool cuda_init(void);

// The actual nebulabrot implementation: one short GPU launch of random
// samples. Returns 0, or a CUDA error code (already logged) after which the
// caller should stop using CUDA for the session.
int cuda_generate_buddhabrot_hits(unsigned int w, unsigned int h,
                                  SupportedFractal & frac,
				  SampleStats & stats,
                                  std::vector<std::vector<long long unsigned int>> &redHits,
                                  std::vector<std::vector<long long unsigned int>> &greenHits,
                                  std::vector<std::vector<long long unsigned int>> &blueHits);

struct cuda_kernel_stats {
  unsigned long long rejected; // skipInSet check
  unsigned long long in_set;
  unsigned long long escaped_set;
  unsigned long long total;
};
