// NOLINT(namespace-envoy)
#include <math.h>

#include <string>

#include "proxy_wasm_intrinsics.h"

// Required Proxy-Wasm ABI version.
extern "C" PROXY_WASM_KEEPALIVE void proxy_abi_version_0_1_0() {}

// Use global variables so the compiler cannot optimize the operations away.
int32_t i32a = 0;
int32_t i32b = 1;
double f64a = 0.0;
double f64b = 1.0;

// Emscripten in some modes and versions would use functions from the `asm2wasm` module to implement
// these operations: int32_t % /, double conversion to int32_t and remainder().
extern "C" PROXY_WASM_KEEPALIVE uint32_t proxy_on_configure(uint32_t, uint32_t) {
  logInfo(std::string("out ") + std::to_string(i32a / i32b) + " " + std::to_string(i32a % i32b) +
          " " + std::to_string((int32_t)remainder(f64a, f64b)));
  return 1;
}
