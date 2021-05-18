// NOLINT(namespace-envoy)
#include <string>

#include "proxy_wasm_intrinsics.h"

template <typename T> std::unique_ptr<T> wrap_unique(T* ptr) { return std::unique_ptr<T>(ptr); }

START_WASM_PLUGIN(WasmHttpCpp)

// Required Proxy-Wasm ABI version.
WASM_EXPORT(void, proxy_abi_version_0_1_0, ()) {}

WASM_EXPORT(uint32_t, proxy_on_configure, (uint32_t, uint32_t)) {
  if (std::getenv("NON_EXIST")) {
    logError("NON_EXIST should not exist in the environment variable key space");
    return 0;
  }
  if (std::string(std::getenv("KEY")) != "VALUE") {
    logError("Environment variable KEY=VALUE must exist");
    return 0;
  }
  proxy_set_tick_period_milliseconds(100);
  return 1;
}

WASM_EXPORT(void, proxy_on_tick, (uint32_t)) {
  HeaderStringPairs headers;
  headers.push_back(std::make_pair<std::string, std::string>(":method", "GET"));
  headers.push_back(std::make_pair<std::string, std::string>(":path", "/"));
  headers.push_back(std::make_pair<std::string, std::string>(":authority", "example.com"));
  headers.push_back(std::make_pair<std::string, std::string>("x-test", "test"));
  HeaderStringPairs trailers;
  uint32_t token;
  WasmResult result = makeHttpCall("wasm_cluster", headers, "", trailers, 10000, &token);
  // We have sent successfully, stop timer - we only want to send one request.
  if (result == WasmResult::Ok) {
    proxy_set_tick_period_milliseconds(0);
  }
}

WASM_EXPORT(void, proxy_on_http_call_response, (uint32_t, uint32_t, uint32_t headers, uint32_t, uint32_t)) {
  logTrace("KEY: " + std::string(std::getenv("KEY")));
  if (headers != 0) {
    auto status = getHeaderMapValue(WasmHeaderMapType::HttpCallResponseHeaders, "status");
    if ("200" == status->view()) {
      proxy_set_tick_period_milliseconds(0);
      return;
    }
  }
  // Request failed - very possibly because of the integration test not being ready.
  // Try again to prevent flakes.
  proxy_set_tick_period_milliseconds(100);
}

END_WASM_PLUGIN
