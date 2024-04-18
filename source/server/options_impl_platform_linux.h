#pragma once

#if !defined(__linux__)
#error "Linux platform file is part of non-Linux build."
#endif

#include <cstdint>
#include <string>

namespace Envoy {
class OptionsImplPlatformLinux {
public:
  static uint32_t getCpuAffinityCount(unsigned int hw_threads);
};
} // namespace Envoy
