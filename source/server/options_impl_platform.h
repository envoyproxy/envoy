#pragma once

#include <cstdint>

#include "common/common/logger.h"

namespace Envoy {
class OptionsImplPlatform : protected Logger::Loggable<Logger::Id::config> {
public:
  static uint32_t getCpuCount();
};
} // namespace Envoy
