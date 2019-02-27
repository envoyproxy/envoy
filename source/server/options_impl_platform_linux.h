#pragma once

#include <cstdint>
#include <string>

namespace Envoy {
class OptionsImplPlatformLinux {
public:
  static uint32_t getCpuAffinityCount(unsigned int hw_threads);
};
} // namespace Envoy
