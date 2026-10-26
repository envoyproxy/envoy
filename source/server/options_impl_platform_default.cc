#include <algorithm>
#include <thread>

#include "source/common/common/logger.h"
#include "source/server/options_impl_platform.h"

namespace Envoy {

uint32_t OptionsImplPlatform::getCpuCount() {
  ENVOY_LOG(debug, "CPU number provided by HW thread count (instead of cpuset).");
  // hardware_concurrency() may return 0 if not computable; floor at 1.
  return std::max(1U, std::thread::hardware_concurrency());
}

} // namespace Envoy
