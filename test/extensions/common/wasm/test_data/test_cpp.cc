// NOLINT(namespace-envoy)
#ifndef WIN32
#include "unistd.h"

#endif
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

#ifndef NULL_PLUGIN
#include "proxy_wasm_intrinsics.h"
#else
#include "include/proxy-wasm/null_plugin.h"
#endif

START_WASM_PLUGIN(CommonWasmTestCpp)

static int* badptr = nullptr;
static float gNan = std::nan("1");
static float gInfinity = INFINITY;
volatile double zero_unbeknownst_to_the_compiler = 0.0;

#ifndef CHECK_RESULT
#define CHECK_RESULT(_c)                                                                           \
  do {                                                                                             \
    if ((_c) != WasmResult::Ok) {                                                                  \
      proxy_log(LogLevel::critical, #_c, sizeof(#_c) - 1);                                         \
      abort();                                                                                     \
    }                                                                                              \
  } while (0)
#endif

#define CHECK_RESULT_NOT_OK(_c)                                                                    \
  do {                                                                                             \
    if ((_c) == WasmResult::Ok) {                                                                  \
      proxy_log(LogLevel::critical, #_c, sizeof(#_c) - 1);                                         \
      abort();                                                                                     \
    }                                                                                              \
  } while (0)

#define FAIL_NOW(_msg)                                                                             \
  do {                                                                                             \
    const std::string __message = _msg;                                                            \
    proxy_log(LogLevel::critical, __message.c_str(), __message.size());                            \
    abort();                                                                                       \
  } while (0)

WASM_EXPORT(void, proxy_abi_version_0_2_1, (void)) {}

WASM_EXPORT(void, proxy_on_context_create, (uint32_t, uint32_t)) {}

WASM_EXPORT(uint32_t, proxy_on_vm_start, (uint32_t context_id, uint32_t configuration_size)) {
  const char* configuration_ptr = nullptr;
  size_t size;
  proxy_get_buffer_bytes(WasmBufferType::VmConfiguration, 0, configuration_size, &configuration_ptr,
                         &size);
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
    LogLevel log_level;
    CHECK_RESULT(proxy_get_log_level(&log_level));
    std::string level_message = "log level is " + std::to_string(static_cast<uint32_t>(log_level));
    proxy_log(LogLevel::info, level_message.c_str(), level_message.size());
  } else if (configuration == "segv") {
    std::string message = "before badptr";
    proxy_log(LogLevel::error, message.c_str(), message.size());
    ::free(const_cast<void*>(reinterpret_cast<const void*>(configuration_ptr)));
    configuration_ptr = nullptr;
    *badptr = 1;
    message = "after badptr";
    proxy_log(LogLevel::error, message.c_str(), message.size());
  } else if (configuration == "divbyzero") {
    std::string message = "before div by zero";
    proxy_log(LogLevel::error, message.c_str(), message.size());
    ::free(const_cast<void*>(reinterpret_cast<const void*>(configuration_ptr)));
    configuration_ptr = nullptr;
    int zero = context_id & 0x100000;
    message = "divide by zero: " + std::to_string(100 / zero);
    proxy_log(LogLevel::error, message.c_str(), message.size());
  } else if (configuration == "globals") {
    std::string message = "NaN " + std::to_string(gNan);
    proxy_log(LogLevel::warn, message.c_str(), message.size());
    message = "inf " + std::to_string(gInfinity);
    proxy_log(LogLevel::warn, message.c_str(), message.size());
    message = "inf " + std::to_string(1.0 / zero_unbeknownst_to_the_compiler);
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
    // Bad type.
    CHECK_RESULT_NOT_OK(
        proxy_define_metric(static_cast<MetricType>(9999), name.data(), name.size(), &c));

    CHECK_RESULT(proxy_increment_metric(c, 1));
    CHECK_RESULT(proxy_increment_metric(g, 1));
    CHECK_RESULT_NOT_OK(proxy_increment_metric(h, 1));
    CHECK_RESULT(proxy_record_metric(g, 2));
    CHECK_RESULT(proxy_record_metric(h, 3));

    uint64_t value;
    // Not found
    CHECK_RESULT_NOT_OK(proxy_get_metric((1 << 10) + 0, &value));
    CHECK_RESULT_NOT_OK(proxy_get_metric((1 << 10) + 1, &value));
    CHECK_RESULT_NOT_OK(proxy_get_metric((1 << 10) + 2, &value));
    CHECK_RESULT_NOT_OK(proxy_get_metric((1 << 10) + 3, &value));
    CHECK_RESULT_NOT_OK(proxy_record_metric((1 << 10) + 0, 1));
    CHECK_RESULT_NOT_OK(proxy_record_metric((1 << 10) + 1, 1));
    CHECK_RESULT_NOT_OK(proxy_record_metric((1 << 10) + 2, 1));
    CHECK_RESULT_NOT_OK(proxy_record_metric((1 << 10) + 3, 1));
    CHECK_RESULT_NOT_OK(proxy_increment_metric((1 << 10) + 0, 1));
    CHECK_RESULT_NOT_OK(proxy_increment_metric((1 << 10) + 1, 1));
    CHECK_RESULT_NOT_OK(proxy_increment_metric((1 << 10) + 2, 1));
    CHECK_RESULT_NOT_OK(proxy_increment_metric((1 << 10) + 3, 1));
    // Found.
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
    // Negative.
    CHECK_RESULT_NOT_OK(proxy_increment_metric(c, -1));
    CHECK_RESULT(proxy_increment_metric(g, -1));
  } else if (configuration == "foreign") {
    std::string function = "compress";
    char* compressed = nullptr;
    size_t compressed_size = 0;
    std::string argument = std::string(2000, 'a'); // super compressible.
    std::string message;
    CHECK_RESULT(proxy_call_foreign_function(function.data(), function.size(), argument.data(),
                                             argument.size(), &compressed, &compressed_size));
    message = std::string("compress ") + std::to_string(argument.size()) + " -> " +
              std::to_string(compressed_size);
    proxy_log(LogLevel::trace, message.c_str(), message.size());
    function = "uncompress";
    char* result = nullptr;
    size_t result_size = 0;
    CHECK_RESULT(proxy_call_foreign_function(function.data(), function.size(), compressed,
                                             compressed_size, &result, &result_size));
    message = std::string("uncompress ") + std::to_string(compressed_size) + " -> " +
              std::to_string(result_size);
    proxy_log(LogLevel::debug, message.c_str(), message.size());
    if (argument != std::string(result, result_size)) {
      message = "compress mismatch ";
      proxy_log(LogLevel::error, message.c_str(), message.size());
    }
    ::free(result);
    result = nullptr;
    memset(compressed, 0, 4); // damage the compressed version.
    if (proxy_call_foreign_function(function.data(), function.size(), compressed, compressed_size,
                                    &result, &result_size) != WasmResult::SerializationFailure) {
      message = "bad uncompress should be an error";
      proxy_log(LogLevel::error, message.c_str(), message.size());
    }
    if (compressed) {
      ::free(compressed);
    }
    if (result) {
      ::free(result);
    }
  } else if (configuration == "configuration") {
    std::string message = "configuration";
    proxy_log(LogLevel::error, message.c_str(), message.size());
  } else if (configuration == "WASI") {
    // These checks depend on Emscripten's support for `WASI` and will only
    // work if invoked on a "real" Wasm VM.
    // Call to clock_time_get on monotonic clock should be available.
    const std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
    int err = fprintf(stdout, "WASI write to stdout\n");
    if (err < 0) {
      FAIL_NOW("stdout write should succeed");
    }
    err = fprintf(stderr, "WASI write to stderr\n");
    if (err < 0) {
      FAIL_NOW("stderr write should succeed");
    }
    // We explicitly don't support reading from stdin
    char tmp[16];
    size_t rc = fread(static_cast<void*>(tmp), 1, 16, stdin);
    if (rc != 0 || errno != ENOSYS) {
      FAIL_NOW("stdin read should fail. errno = " + std::to_string(errno));
    }
    // No environment variables should be available
    char* pathenv = getenv("PATH");
    if (pathenv != nullptr) {
      FAIL_NOW("PATH environment variable should not be available");
    }
    // Check if the monotonic clock actually increases monotonically.
    const std::chrono::steady_clock::time_point t2 = std::chrono::steady_clock::now();
    if ((t2-t1).count() <= 0) {
      FAIL_NOW("monotonic clock should be available");
    }
#ifndef WIN32
    // Exercise the `WASI` `fd_fdstat_get` a little bit
    int tty = isatty(1);
    if (errno != ENOTTY || tty != 0) {
      FAIL_NOW("stdout is not a tty");
    }
    tty = isatty(2);
    if (errno != ENOTTY || tty != 0) {
      FAIL_NOW("stderr is not a tty");
    }
    tty = isatty(99);
    if (errno != EBADF || tty != 0) {
      FAIL_NOW("isatty errors on bad fds. errno = " + std::to_string(errno));
    }
#endif
  } else if (configuration == "on_foreign") {
    std::string message = "on_foreign start";
    proxy_log(LogLevel::debug, message.c_str(), message.size());
  } else {
    std::string message = "on_vm_start " + configuration;
    proxy_log(LogLevel::info, message.c_str(), message.size());
  }
  ::free(const_cast<void*>(reinterpret_cast<const void*>(configuration_ptr)));
  return 1;
}

WASM_EXPORT(uint32_t, proxy_on_configure, (uint32_t, uint32_t configuration_size)) {
  const char* configuration_ptr = nullptr;
  size_t size;
  proxy_get_buffer_bytes(WasmBufferType::PluginConfiguration, 0, configuration_size,
                         &configuration_ptr, &size);
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

WASM_EXPORT(void, proxy_on_foreign_function, (uint32_t, uint32_t token, uint32_t data_size)) {
  std::string message =
      "on_foreign_function " + std::to_string(token) + " " + std::to_string(data_size);
  proxy_log(LogLevel::info, message.c_str(), message.size());
}

WASM_EXPORT(uint32_t, proxy_on_done, (uint32_t)) {
  std::string message = "on_done logging";
  proxy_log(LogLevel::info, message.c_str(), message.size());
  return 0;
}

WASM_EXPORT(void, proxy_on_tick, (uint32_t)) {
  proxy_done();
}

WASM_EXPORT(void, proxy_on_delete, (uint32_t)) {
  std::string message = "on_delete logging";
  proxy_log(LogLevel::info, message.c_str(), message.size());
}

END_WASM_PLUGIN
