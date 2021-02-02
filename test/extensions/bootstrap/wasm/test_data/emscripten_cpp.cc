// NOLINT(namespace-envoy)
#include <cmath>
#include <limits>
#include <string>

#include "proxy_wasm_intrinsics.h"

// Required Proxy-Wasm ABI version.
extern "C" PROXY_WASM_KEEPALIVE void proxy_abi_version_0_1_0() {}

float gNan = std::nan("1");
float gInfinity = INFINITY;

extern "C" PROXY_WASM_KEEPALIVE uint32_t proxy_on_configure(uint32_t, uint32_t) {
  logInfo(std::string("NaN ") + std::to_string(gNan));
  logWarn("inf " + std::to_string(gInfinity));
  logWarn("inf " + std::to_string(1.0 / 0.0));
  logWarn(std::string("inf ") + (std::isinf(gInfinity) ? "inf" : "nan"));
  return 1;
}
