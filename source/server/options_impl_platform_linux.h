#pragma once

#include <cstdint>
#include <string>

namespace Envoy {
class OptionsImplPlatformLinux {
public:
  static uint32_t getCpuCountFromPath(std::string& file, unsigned int hw_threads);
};
} // namespace Envoy
