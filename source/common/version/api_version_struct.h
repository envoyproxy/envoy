#pragma once
#include <cstdint>

namespace Envoy {

/**
 * Api Version is defined by a <major>.<minor>.<patch> versions.
 */
struct ApiVersion {
  uint32_t major;
  uint32_t minor;
  uint32_t patch;
};

} // namespace Envoy
