#include <thread>

#include "source/common/common/logger.h"
#include "source/server/options_impl_platform.h"

namespace Envoy {

uint32_t OptionsImplPlatform::getCpuCount() {
  ENVOY_LOG(debug, "CPU number provided by HW thread count (instead of cpuset).");
  return std::thread::hardware_concurrency();
}

} // namespace Envoy
