// NOLINT(namespace-envoy)
#include <cmath>
#include <limits>
#include <string>

#ifndef NULL_PLUGIN
#include "proxy_wasm_intrinsics.h"
#else
#include "extensions/common/wasm/null/null_plugin.h"
#endif

START_WASM_PLUGIN(CommonWasmTestCpp)

static int* badptr = nullptr;
static float gNan = std::nan("1");
static float gInfinity = INFINITY;

#ifndef CHECK_RESULT
#define CHECK_RESULT(_c)                                                                           \
  do {                                                                                             \
    if ((_c) != WasmResult::Ok) {                                                                  \
      proxy_log(LogLevel::critical, #_c, sizeof(#_c) - 1);                                         \
      abort();                                                                                     \
    }                                                                                              \
  } while (0)
#endif

WASM_EXPORT(uint32_t, proxy_on_start, (uint32_t, uint32_t context_id)) {
  const char* configuration_ptr = nullptr;
  size_t size;
  proxy_get_configuration(&configuration_ptr, &size);
  std::string configuration(configuration_ptr, size);
  if (configuration == "logging") {
    std::string trace_message = "test trace logging";
    proxy_log(LogLevel::trace, trace_message.c_str(), trace_message.size());
    std::string debug_message = "test debug logging";
    proxy_log(LogLevel::debug, debug_message.c_str(), debug_message.size());
    std::string warn_message = "test warn logging";
    proxy_log(LogLevel::warn, warn_message.c_str(), warn_message.size());
    std::string error_message = "test error logging";
    proxy_log(LogLevel::error, error_message.c_str(), error_message.size());
  } else if (configuration == "segv") {
    std::string message = "before badptr";
    proxy_log(LogLevel::error, message.c_str(), message.size());
    ::free(const_cast<void*>(reinterpret_cast<const void*>(configuration_ptr)));
    *badptr = 1;
    message = "after badptr";
    proxy_log(LogLevel::error, message.c_str(), message.size());
  } else if (configuration == "divbyzero") {
    std::string message = "before div by zero";
    proxy_log(LogLevel::error, message.c_str(), message.size());
    int zero = context_id / 1000;
    ::free(const_cast<void*>(reinterpret_cast<const void*>(configuration_ptr)));
    message = "divide by zero: " + std::to_string(100 / zero);
    proxy_log(LogLevel::error, message.c_str(), message.size());
  } else if (configuration == "globals") {
    std::string message = "NaN " + std::to_string(gNan);
    proxy_log(LogLevel::warn, message.c_str(), message.size());
    message = "inf " + std::to_string(gInfinity);
    proxy_log(LogLevel::warn, message.c_str(), message.size());
    message = "inf " + std::to_string(1.0 / 0.0);
    proxy_log(LogLevel::warn, message.c_str(), message.size());
    message = std::string("inf ") + (std::isinf(gInfinity) ? "inf" : "nan");
    proxy_log(LogLevel::warn, message.c_str(), message.size());
  } else if (configuration == "stats") {
    uint32_t c, g, h;

    std::string name = "test_counter";
    CHECK_RESULT(proxy_define_metric(MetricType::Counter, name.data(), name.size(), &c));
    name = "test_gauge";
    CHECK_RESULT(proxy_define_metric(MetricType::Gauge, name.data(), name.size(), &g));
    name = "test_historam";
    CHECK_RESULT(proxy_define_metric(MetricType::Histogram, name.data(), name.size(), &h));

    CHECK_RESULT(proxy_increment_metric(c, 1));
    CHECK_RESULT(proxy_record_metric(g, 2));
    CHECK_RESULT(proxy_record_metric(h, 3));

    uint64_t value;
    std::string message;
    CHECK_RESULT(proxy_get_metric(c, &value));
    message = std::string("get counter = ") + std::to_string(value);
    proxy_log(LogLevel::trace, message.c_str(), message.size());
    CHECK_RESULT(proxy_increment_metric(c, 1));
    CHECK_RESULT(proxy_get_metric(c, &value));
    message = std::string("get counter = ") + std::to_string(value);
    proxy_log(LogLevel::debug, message.c_str(), message.size());
    CHECK_RESULT(proxy_record_metric(c, 3));
    CHECK_RESULT(proxy_get_metric(c, &value));
    message = std::string("get counter = ") + std::to_string(value);
    proxy_log(LogLevel::info, message.c_str(), message.size());
    CHECK_RESULT(proxy_get_metric(g, &value));
    message = std::string("get gauge = ") + std::to_string(value);
    proxy_log(LogLevel::warn, message.c_str(), message.size());
    // Get on histograms is not supported.
    if (proxy_get_metric(h, &value) != WasmResult::Ok) {
      message = std::string("get histogram = Unsupported");
      proxy_log(LogLevel::error, message.c_str(), message.size());
    }
  } else {
    std::string message = "on_start " + configuration;
    proxy_log(LogLevel::info, message.c_str(), message.size());
  }
  ::free(const_cast<void*>(reinterpret_cast<const void*>(configuration_ptr)));
  return 1;
}

WASM_EXPORT(uint32_t, proxy_on_configure, (uint32_t, uint32_t)) {
  const char* configuration_ptr = nullptr;
  size_t size;
  proxy_get_configuration(&configuration_ptr, &size);
  std::string configuration(configuration_ptr, size);
  if (configuration == "done") {
    proxy_done();
  } else {
    std::string message = "on_configuration " + configuration;
    proxy_log(LogLevel::info, message.c_str(), message.size());
  }
  ::free(const_cast<void*>(reinterpret_cast<const void*>(configuration_ptr)));
  return 1;
}

WASM_EXPORT(uint32_t, proxy_on_done, (uint32_t)) {
  std::string message = "on_done logging";
  proxy_log(LogLevel::info, message.c_str(), message.size());
  return 0;
}

WASM_EXPORT(void, proxy_on_delete, (uint32_t)) {
  std::string message = "on_delete logging";
  proxy_log(LogLevel::info, message.c_str(), message.size());
}

END_WASM_PLUGIN
