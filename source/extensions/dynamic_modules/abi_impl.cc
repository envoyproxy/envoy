// NOLINT(namespace-envoy)

// This file provides host-side implementations for ABI callbacks that are shared across
// all dynamic modules. These are the "Common Callbacks" declared in abi.h and are available
// regardless of which extension point is being used (HTTP/Network/Listener/UDP/Bootstrap/etc).

#include <string>

#include "envoy/runtime/runtime.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/extensions/dynamic_modules/abi/abi.h"

#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"

namespace {

// Process-wide function registry. Modules register function pointers by name during bootstrap,
// and other modules resolve them by name during configuration creation.
absl::Mutex function_registry_mutex;
absl::flat_hash_map<std::string, void*> function_registry ABSL_GUARDED_BY(function_registry_mutex);

// Process-wide shared data registry. Modules register opaque data pointers by name during
// bootstrap, and other modules resolve them by name during configuration creation. Unlike the
// function registry, this allows overwriting existing entries.
absl::Mutex shared_data_registry_mutex;
absl::flat_hash_map<std::string, void*>
    shared_data_registry ABSL_GUARDED_BY(shared_data_registry_mutex);

// Logs message to the dynamic modules logger at level, dropping levels outside the valid spdlog
// range or below the configured level. The deprecated callback passes an empty source location.
void logToDynamicModulesLogger(envoy_dynamic_module_type_log_level level,
                               envoy_dynamic_module_type_module_buffer message,
                               spdlog::source_loc source_location) {
  spdlog::logger& logger = Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules);
  const auto spdlog_level = static_cast<spdlog::level::level_enum>(level);
  // Ignore Off and any out-of-range value, which also guards spdlog against an invalid level.
  if (spdlog_level < spdlog::level::trace || spdlog_level > spdlog::level::critical) {
    return;
  }
  if (!Envoy::Logger::should_log || spdlog_level < logger.level()) {
    return;
  }
  absl::string_view message_view(message.ptr, message.length);
  logger.log(source_location, spdlog_level, "{}", message_view);
}

// Resolve the runtime snapshot for the calling thread, or nullptr when the server context is not
// installed on this thread. Unlike the main-thread-only callbacks below this does not fail closed:
// the server context is a thread local singleton that only exists on the main thread, so a call
// from a worker thread legitimately finds no context and must fall back to the caller's default
// rather than trip an ENVOY_BUG on every lookup.
Envoy::OptRef<const Envoy::Runtime::Snapshot> currentRuntimeSnapshot() {
  auto context = Envoy::Server::Configuration::ServerFactoryContextInstance::getExisting();
  if (context == nullptr) {
    return {};
  }
  return context->runtime().snapshot();
}

} // namespace

extern "C" {

bool envoy_dynamic_module_callback_log_enabled(envoy_dynamic_module_type_log_level level) {
  return Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules).level() <=
         static_cast<spdlog::level::level_enum>(level);
}

void envoy_dynamic_module_callback_log(envoy_dynamic_module_type_log_level level,
                                       envoy_dynamic_module_type_module_buffer message) {
  // The deprecated callback predates source location support, so report an empty location.
  logToDynamicModulesLogger(level, message, spdlog::source_loc{});
}

void envoy_dynamic_module_callback_log_v2(envoy_dynamic_module_type_log_level level,
                                          envoy_dynamic_module_type_module_buffer message,
                                          envoy_dynamic_module_type_module_buffer source_file,
                                          uint32_t source_line) {
  // spdlog reads the source file as a null-terminated C string. Reuse a thread-local buffer to
  // null-terminate the module-owned bytes without allocating on each log call. The pointer only
  // needs to stay valid for this synchronous log call.
  thread_local std::string source_file_buffer;
  if (source_file.ptr == nullptr) {
    source_file_buffer.clear();
  } else {
    source_file_buffer.assign(source_file.ptr, source_file.length);
  }
  // Log directly with the module source location. The ENVOY_LOG macros would bake in the
  // abi_impl.cc file and line instead.
  logToDynamicModulesLogger(
      level, message,
      spdlog::source_loc{source_file_buffer.c_str(), static_cast<int>(source_line), ""});
}

envoy_dynamic_module_type_log_level envoy_dynamic_module_callback_get_log_level() {
  // The ABI log level enum mirrors spdlog::level::level_enum, so the cast is a direct mapping.
  return static_cast<envoy_dynamic_module_type_log_level>(
      Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules).level());
}

uint32_t envoy_dynamic_module_callback_get_concurrency() {
  using namespace Envoy;
  // The previous `ASSERT_IS_MAIN_OR_TEST_THREAD` is compiled out under NDEBUG and the
  // `getExisting()` thread-local lookup returns nullptr off the main thread, so guard explicitly
  // and fail closed.
  if (!Thread::MainThread::isMainOrTestThread()) {
    IS_ENVOY_BUG("envoy_dynamic_module_callback_get_concurrency must be called on the main thread");
    return 0;
  }
  auto context = Server::Configuration::ServerFactoryContextInstance::getExisting();
  if (context == nullptr) {
    IS_ENVOY_BUG("envoy_dynamic_module_callback_get_concurrency called before the server context "
                 "was initialized");
    return 0;
  }
  return context->options().concurrency();
}

bool envoy_dynamic_module_callback_is_validation_mode() {
  using namespace Envoy;
  if (!Thread::MainThread::isMainOrTestThread()) {
    IS_ENVOY_BUG(
        "envoy_dynamic_module_callback_is_validation_mode must be called on the main thread");
    return false;
  }
  auto context = Server::Configuration::ServerFactoryContextInstance::getExisting();
  if (context == nullptr) {
    IS_ENVOY_BUG("envoy_dynamic_module_callback_is_validation_mode called before the server "
                 "context was initialized");
    return false;
  }
  return context->options().mode() == Server::Mode::Validate;
}

// ---------------------- Runtime callbacks --------------------------------

bool envoy_dynamic_module_callback_get_runtime_bool(envoy_dynamic_module_type_module_buffer key,
                                                    bool default_value) {
  const auto snapshot = currentRuntimeSnapshot();
  if (!snapshot.has_value()) {
    return default_value;
  }
  return snapshot->getBoolean(absl::string_view(key.ptr, key.length), default_value);
}

uint64_t envoy_dynamic_module_callback_get_runtime_int(envoy_dynamic_module_type_module_buffer key,
                                                       uint64_t default_value) {
  const auto snapshot = currentRuntimeSnapshot();
  if (!snapshot.has_value()) {
    return default_value;
  }
  return snapshot->getInteger(absl::string_view(key.ptr, key.length), default_value);
}

double envoy_dynamic_module_callback_get_runtime_number(envoy_dynamic_module_type_module_buffer key,
                                                        double default_value) {
  const auto snapshot = currentRuntimeSnapshot();
  if (!snapshot.has_value()) {
    return default_value;
  }
  return snapshot->getDouble(absl::string_view(key.ptr, key.length), default_value);
}

// ---------------------- Function registry callbacks --------------------------------

bool envoy_dynamic_module_callback_register_function(envoy_dynamic_module_type_module_buffer key,
                                                     void* function_ptr) {
  if (function_ptr == nullptr) {
    return false;
  }
  absl::WriterMutexLock lock(function_registry_mutex);
  auto [it, inserted] =
      function_registry.try_emplace(std::string(key.ptr, key.length), function_ptr);
  return inserted;
}

bool envoy_dynamic_module_callback_get_function(envoy_dynamic_module_type_module_buffer key,
                                                void** function_ptr_out) {
  absl::ReaderMutexLock lock(function_registry_mutex);
  auto it = function_registry.find(absl::string_view(key.ptr, key.length));
  if (it != function_registry.end()) {
    *function_ptr_out = it->second;
    return true;
  }
  return false;
}

// ---------------------- Shared data registry callbacks --------------------------------

bool envoy_dynamic_module_callback_register_shared_data(envoy_dynamic_module_type_module_buffer key,
                                                        void* data_ptr) {
  if (data_ptr == nullptr) {
    return false;
  }
  absl::WriterMutexLock lock(shared_data_registry_mutex);
  shared_data_registry[std::string(key.ptr, key.length)] = data_ptr;
  return true;
}

bool envoy_dynamic_module_callback_get_shared_data(envoy_dynamic_module_type_module_buffer key,
                                                   void** data_ptr_out) {
  absl::ReaderMutexLock lock(shared_data_registry_mutex);
  auto it = shared_data_registry.find(absl::string_view(key.ptr, key.length));
  if (it != shared_data_registry.end()) {
    *data_ptr_out = it->second;
    return true;
  }
  return false;
}

// ---------------------- Bootstrap extension scheduler callbacks ------------------------
// These are weak symbols that provide default stub implementations. The actual implementations
// are provided in the bootstrap extension abi_impl.cc when the bootstrap extension is used.
// This is necessary because the Rust SDK generates bindings for all callbacks in abi.h, and
// these symbols must be resolvable when any Rust module is loaded.
//
// We use IS_ENVOY_BUG instead of PANIC to allow coverage collection in tests. In non-coverage
// debug builds, IS_ENVOY_BUG will abort; in coverage builds it logs and continues, allowing the
// test to verify the error path was hit.

// These macros collapse the repetitive weak callback stubs below. Each ABI callback is defined
// weak so an unimplemented host callback fails closed at runtime instead of failing to link. The
// return type and fail-closed default must be comma-free so each parses as one macro argument, and
// the default is an explicit argument so every stub keeps its exact value, including the non-zero
// results such as `MetricNotFound`.
#define WEAK_STUB(ret, name, default_value, ...)                                                   \
  __attribute__((weak)) ret name(__VA_ARGS__) {                                                    \
    IS_ENVOY_BUG(#name ": not implemented in this context");                                       \
    return default_value;                                                                          \
  }
#define WEAK_STUB_VOID(name, ...)                                                                  \
  __attribute__((weak)) void name(__VA_ARGS__) {                                                   \
    IS_ENVOY_BUG(#name ": not implemented in this context");                                       \
  }

WEAK_STUB(envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr,
          envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new, nullptr,
          envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete,
               envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_commit,
               envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr, uint64_t)

WEAK_STUB_VOID(envoy_dynamic_module_callback_bootstrap_extension_config_signal_init_complete,
               envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr)

WEAK_STUB(envoy_dynamic_module_type_http_callout_init_result,
          envoy_dynamic_module_callback_bootstrap_extension_http_callout,
          envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest,
          envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
          uint64_t* /* callout_id_out */,
          envoy_dynamic_module_type_module_buffer /* cluster_name */,
          envoy_dynamic_module_type_module_http_header* /* headers */, size_t /* headers_size */,
          envoy_dynamic_module_type_module_buffer /* body */, uint64_t /* timeout_milliseconds */)

// ---------------------- Bootstrap extension stats access callbacks ------------------------
// These are weak symbols that provide default stub implementations. The actual implementations
// are provided in the bootstrap extension abi_impl.cc when the bootstrap extension is used.

WEAK_STUB(bool, envoy_dynamic_module_callback_bootstrap_extension_get_counter_value, false,
          envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, uint64_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_bootstrap_extension_get_gauge_value, false,
          envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, uint64_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_bootstrap_extension_get_histogram_summary, false,
          envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, uint64_t*, double*)

WEAK_STUB_VOID(envoy_dynamic_module_callback_bootstrap_extension_iterate_counters,
               envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
               envoy_dynamic_module_type_counter_iterator_fn, void*)

WEAK_STUB_VOID(envoy_dynamic_module_callback_bootstrap_extension_iterate_gauges,
               envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
               envoy_dynamic_module_type_gauge_iterator_fn, void*)

// ---------------------- Bootstrap extension stats definition and update callbacks
// --------------------- These are weak symbols that provide default stub implementations. The
// actual implementations are provided in the bootstrap extension abi_impl.cc when the bootstrap
// extension is used.

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_bootstrap_extension_config_define_counter,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer*, size_t,
          size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_bootstrap_extension_config_increment_counter,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_bootstrap_extension_config_define_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer*, size_t,
          size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_bootstrap_extension_config_set_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_bootstrap_extension_config_increment_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_bootstrap_extension_config_decrement_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_bootstrap_extension_config_define_histogram,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer*, size_t,
          size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_bootstrap_extension_config_record_histogram_value,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

// ---------------------- Cert Validator callbacks ------------------------
// These are weak symbols that provide default stub implementations. The actual implementation
// is provided in the cert validator config.cc when the cert validator extension is used.

WEAK_STUB_VOID(envoy_dynamic_module_callback_cert_validator_set_error_details,
               envoy_dynamic_module_type_cert_validator_config_envoy_ptr,
               envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_cert_validator_set_filter_state, false,
          envoy_dynamic_module_type_cert_validator_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_cert_validator_get_filter_state, false,
          envoy_dynamic_module_type_cert_validator_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_envoy_buffer*)

// ---------------------- Config Validator callbacks ------------------------
// These are weak symbols that provide default stub implementations. The actual implementation is
// provided in the config validator dynamic module extension when it is used.

__attribute__((weak)) void envoy_dynamic_module_callback_config_validator_set_rejection_message(
    envoy_dynamic_module_type_config_validator_context_envoy_ptr,
    envoy_dynamic_module_type_module_buffer) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_config_validator_set_rejection_message: "
               "not implemented in this context");
}

__attribute__((weak)) uint64_t
envoy_dynamic_module_callback_config_validator_get_dynamic_cluster_count(
    envoy_dynamic_module_type_config_validator_context_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_config_validator_get_dynamic_cluster_count: "
               "not implemented in this context");
  return 0;
}

// ---------------------- Bootstrap extension admin handler callbacks ------------------------
// These are weak symbols that provide default stub implementations. The actual implementations
// are provided in the bootstrap extension abi_impl.cc when the bootstrap extension is used.

WEAK_STUB_VOID(envoy_dynamic_module_callback_bootstrap_extension_admin_set_response,
               envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
               envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_bootstrap_extension_register_admin_handler, false,
          envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer, bool,
          bool)

WEAK_STUB(bool, envoy_dynamic_module_callback_bootstrap_extension_remove_admin_handler, false,
          envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer)

// ---------------------- Bootstrap extension timer callbacks ------------------------
// These are weak symbols that provide default stub implementations. The actual implementations
// are provided in the bootstrap extension abi_impl.cc when the bootstrap extension is used.

WEAK_STUB(envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr,
          envoy_dynamic_module_callback_bootstrap_extension_timer_new, nullptr,
          envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_bootstrap_extension_timer_enable,
               envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr, uint64_t)

WEAK_STUB_VOID(envoy_dynamic_module_callback_bootstrap_extension_timer_disable,
               envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_bootstrap_extension_timer_enabled, false,
          envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_bootstrap_extension_timer_delete,
               envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_bootstrap_extension_file_watcher_add_watch, false,
          envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, uint32_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_bootstrap_extension_enable_cluster_lifecycle, false,
          envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_bootstrap_extension_enable_listener_lifecycle, false,
          envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr)

// ---------------------- Cluster extension callbacks ------------------------
// These are weak symbols that provide default stub implementations. The actual implementations
// are provided in the cluster extension abi_impl.cc when the cluster extension is used.

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_add_hosts, false,
          envoy_dynamic_module_type_cluster_envoy_ptr, uint32_t,
          const envoy_dynamic_module_type_module_buffer*, const uint32_t*,
          const envoy_dynamic_module_type_module_buffer*,
          const envoy_dynamic_module_type_module_buffer*,
          const envoy_dynamic_module_type_module_buffer*,
          const envoy_dynamic_module_type_module_buffer*, size_t, size_t,
          envoy_dynamic_module_type_cluster_host_envoy_ptr*)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_add_hosts_with_hostnames, false,
          envoy_dynamic_module_type_cluster_envoy_ptr, uint32_t,
          const envoy_dynamic_module_type_module_buffer*,
          const envoy_dynamic_module_type_module_buffer*, const uint32_t*,
          const envoy_dynamic_module_type_module_buffer*,
          const envoy_dynamic_module_type_module_buffer*,
          const envoy_dynamic_module_type_module_buffer*,
          const envoy_dynamic_module_type_module_buffer*, size_t, size_t,
          envoy_dynamic_module_type_cluster_host_envoy_ptr*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_cluster_remove_hosts, 0,
          envoy_dynamic_module_type_cluster_envoy_ptr,
          const envoy_dynamic_module_type_cluster_host_envoy_ptr*, size_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_update_host_health, false,
          envoy_dynamic_module_type_cluster_envoy_ptr,
          envoy_dynamic_module_type_cluster_host_envoy_ptr, envoy_dynamic_module_type_host_health)

WEAK_STUB(envoy_dynamic_module_type_cluster_host_envoy_ptr,
          envoy_dynamic_module_callback_cluster_find_host_by_address, nullptr,
          envoy_dynamic_module_type_cluster_envoy_ptr, envoy_dynamic_module_type_module_buffer)

WEAK_STUB(envoy_dynamic_module_type_cluster_host_envoy_ptr,
          envoy_dynamic_module_callback_cluster_lb_find_host_by_address, nullptr,
          envoy_dynamic_module_type_cluster_lb_envoy_ptr, envoy_dynamic_module_type_module_buffer)

WEAK_STUB(envoy_dynamic_module_type_cluster_host_envoy_ptr,
          envoy_dynamic_module_callback_cluster_lb_get_host, nullptr,
          envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_lb_get_member_update_host_address, false,
          envoy_dynamic_module_type_cluster_lb_envoy_ptr, size_t, bool,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(envoy_dynamic_module_type_cluster_host_envoy_ptr,
          envoy_dynamic_module_callback_cluster_lb_get_member_update_host, nullptr,
          envoy_dynamic_module_type_cluster_lb_envoy_ptr, size_t, bool)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_lb_get_member_update_host_packed_address,
          false, envoy_dynamic_module_type_cluster_lb_envoy_ptr, size_t, bool,
          envoy_dynamic_module_type_packed_address*)

WEAK_STUB_VOID(envoy_dynamic_module_callback_cluster_pre_init_complete,
               envoy_dynamic_module_type_cluster_envoy_ptr)

WEAK_STUB(size_t, envoy_dynamic_module_callback_cluster_lb_get_healthy_host_count, 0,
          envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t)

WEAK_STUB(envoy_dynamic_module_type_cluster_host_envoy_ptr,
          envoy_dynamic_module_callback_cluster_lb_get_healthy_host, nullptr,
          envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_lb_get_healthy_hosts, false,
          envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t,
          envoy_dynamic_module_type_cluster_host_envoy_ptr*, size_t, size_t*)

WEAK_STUB_VOID(envoy_dynamic_module_callback_cluster_lb_get_cluster_name,
               envoy_dynamic_module_type_cluster_lb_envoy_ptr,
               envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_cluster_lb_get_hosts_count, 0,
          envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t)

WEAK_STUB(size_t, envoy_dynamic_module_callback_cluster_lb_get_degraded_hosts_count, 0,
          envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t)

WEAK_STUB(size_t, envoy_dynamic_module_callback_cluster_lb_get_priority_set_size, 0,
          envoy_dynamic_module_type_cluster_lb_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_lb_get_healthy_host_address, false,
          envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(uint32_t, envoy_dynamic_module_callback_cluster_lb_get_healthy_host_weight, 0,
          envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t)

WEAK_STUB(envoy_dynamic_module_type_host_health,
          envoy_dynamic_module_callback_cluster_lb_get_host_health,
          envoy_dynamic_module_type_host_health_Unhealthy,
          envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_lb_get_host_health_by_address, false,
          envoy_dynamic_module_type_cluster_lb_envoy_ptr, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_host_health*)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_lb_get_host_address, false,
          envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(uint32_t, envoy_dynamic_module_callback_cluster_lb_get_host_weight, 0,
          envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t)

WEAK_STUB(uint64_t, envoy_dynamic_module_callback_cluster_lb_get_host_stat, 0,
          envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t,
          envoy_dynamic_module_type_host_stat)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_lb_get_host_locality, false,
          envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t,
          envoy_dynamic_module_type_envoy_buffer*, envoy_dynamic_module_type_envoy_buffer*,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_lb_set_host_data, false,
          envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t, uintptr_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_lb_get_host_data, false,
          envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t, uintptr_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_lb_get_host_metadata_string, false,
          envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_lb_get_host_metadata_number, false,
          envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer, double*)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_lb_get_host_metadata_bool, false,
          envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer, bool*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_cluster_lb_get_locality_count, 0,
          envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t)

WEAK_STUB(size_t, envoy_dynamic_module_callback_cluster_lb_get_locality_host_count, 0,
          envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_lb_get_locality_host_address, false,
          envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t, size_t,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(uint32_t, envoy_dynamic_module_callback_cluster_lb_get_locality_weight, 0,
          envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_lb_context_compute_hash_key, false,
          envoy_dynamic_module_type_cluster_lb_context_envoy_ptr, uint64_t*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers_size, 0,
          envoy_dynamic_module_type_cluster_lb_context_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers, false,
          envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
          envoy_dynamic_module_type_envoy_http_header*)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_lb_context_get_downstream_header, false,
          envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_envoy_buffer*, size_t,
          size_t*)

WEAK_STUB(uint32_t, envoy_dynamic_module_callback_cluster_lb_context_get_host_selection_retry_count,
          0, envoy_dynamic_module_type_cluster_lb_context_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_lb_context_should_select_another_host, false,
          envoy_dynamic_module_type_cluster_lb_envoy_ptr,
          envoy_dynamic_module_type_cluster_lb_context_envoy_ptr, uint32_t, size_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_lb_context_get_override_host, false,
          envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*, bool*)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_lb_context_get_downstream_connection_sni,
          false, envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_lb_context_get_filter_state_bytes, false,
          envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_lb_context_get_filter_state_typed, false,
          envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_lb_context_set_filter_state_bytes, false,
          envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_lb_context_set_filter_state_typed, false,
          envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer)

WEAK_STUB(uint64_t, envoy_dynamic_module_callback_cluster_lb_context_get_host_stat, 0,
          envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
          envoy_dynamic_module_type_cluster_host_envoy_ptr, envoy_dynamic_module_type_host_stat)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_lb_context_set_dynamic_metadata_number, false,
          envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer, double)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_lb_context_set_dynamic_metadata_string, false,
          envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_lb_context_set_dynamic_metadata_string_batch,
          false, envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
          envoy_dynamic_module_type_module_buffer,
          const envoy_dynamic_module_type_module_key_value_pair*, size_t)

WEAK_STUB(envoy_dynamic_module_type_cluster_scheduler_module_ptr,
          envoy_dynamic_module_callback_cluster_scheduler_new, nullptr,
          envoy_dynamic_module_type_cluster_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_cluster_scheduler_delete,
               envoy_dynamic_module_type_cluster_scheduler_module_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_cluster_scheduler_commit,
               envoy_dynamic_module_type_cluster_scheduler_module_ptr, uint64_t)

WEAK_STUB_VOID(envoy_dynamic_module_callback_cluster_run_on_all_workers,
               envoy_dynamic_module_type_cluster_envoy_ptr, uint64_t)

WEAK_STUB_VOID(envoy_dynamic_module_callback_cluster_worker_slot_set,
               envoy_dynamic_module_type_cluster_envoy_ptr,
               envoy_dynamic_module_type_cluster_worker_slot_data_module_ptr)

WEAK_STUB(envoy_dynamic_module_type_cluster_worker_slot_data_module_ptr,
          envoy_dynamic_module_callback_cluster_worker_slot_get, nullptr,
          envoy_dynamic_module_type_cluster_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_cluster_get_name,
               envoy_dynamic_module_type_cluster_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*)

// ---- Cluster worker timer callbacks ----

WEAK_STUB(envoy_dynamic_module_type_cluster_worker_timer_module_ptr,
          envoy_dynamic_module_callback_cluster_worker_timer_new, nullptr,
          envoy_dynamic_module_type_cluster_lb_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_cluster_worker_timer_enable,
               envoy_dynamic_module_type_cluster_worker_timer_module_ptr, uint64_t)

WEAK_STUB_VOID(envoy_dynamic_module_callback_cluster_worker_timer_disable,
               envoy_dynamic_module_type_cluster_worker_timer_module_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_worker_timer_enabled, false,
          envoy_dynamic_module_type_cluster_worker_timer_module_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_cluster_worker_timer_delete,
               envoy_dynamic_module_type_cluster_worker_timer_module_ptr)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_cluster_config_define_counter,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_cluster_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer*, size_t,
          size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_cluster_config_increment_counter,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_cluster_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_cluster_config_define_gauge,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_cluster_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer*, size_t,
          size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_cluster_config_set_gauge,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_cluster_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_cluster_config_increment_gauge,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_cluster_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_cluster_config_decrement_gauge,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_cluster_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_cluster_config_define_histogram,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_cluster_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer*, size_t,
          size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_cluster_config_record_histogram_value,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_cluster_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_cluster_config_resolve_counter_vec,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_cluster_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t,
          envoy_dynamic_module_type_cluster_metric_counter_envoy_ptr*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_cluster_config_resolve_gauge_vec,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_cluster_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t,
          envoy_dynamic_module_type_cluster_metric_gauge_envoy_ptr*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_cluster_config_resolve_histogram_vec,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_cluster_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t,
          envoy_dynamic_module_type_cluster_metric_histogram_envoy_ptr*)

WEAK_STUB_VOID(envoy_dynamic_module_callback_cluster_metric_counter_add,
               envoy_dynamic_module_type_cluster_metric_counter_envoy_ptr, uint64_t)

WEAK_STUB_VOID(envoy_dynamic_module_callback_cluster_metric_gauge_set,
               envoy_dynamic_module_type_cluster_metric_gauge_envoy_ptr, uint64_t)

WEAK_STUB_VOID(envoy_dynamic_module_callback_cluster_metric_gauge_add,
               envoy_dynamic_module_type_cluster_metric_gauge_envoy_ptr, uint64_t)

WEAK_STUB_VOID(envoy_dynamic_module_callback_cluster_metric_gauge_sub,
               envoy_dynamic_module_type_cluster_metric_gauge_envoy_ptr, uint64_t)

WEAK_STUB_VOID(envoy_dynamic_module_callback_cluster_metric_histogram_record,
               envoy_dynamic_module_type_cluster_metric_histogram_envoy_ptr, uint64_t)

WEAK_STUB_VOID(envoy_dynamic_module_callback_cluster_lb_async_host_selection_complete,
               envoy_dynamic_module_type_cluster_lb_envoy_ptr,
               envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
               envoy_dynamic_module_type_cluster_host_envoy_ptr,
               envoy_dynamic_module_type_module_buffer)

WEAK_STUB(envoy_dynamic_module_type_http_callout_init_result,
          envoy_dynamic_module_callback_cluster_http_callout,
          envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest,
          envoy_dynamic_module_type_cluster_envoy_ptr, uint64_t* /* callout_id_out */,
          envoy_dynamic_module_type_module_buffer /* cluster_name */,
          envoy_dynamic_module_type_module_http_header* /* headers */, size_t /* headers_size */,
          envoy_dynamic_module_type_module_buffer /* body */, uint64_t /* timeout_milliseconds */)

// ---------------------- Load Balancer callbacks ------------------------
// These are weak symbols that provide default stub implementations. The actual implementations
// are provided in the load balancing policy extension abi_impl.cc when the extension is used.

WEAK_STUB_VOID(envoy_dynamic_module_callback_lb_get_cluster_name,
               envoy_dynamic_module_type_lb_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_lb_get_hosts_count, 0,
          envoy_dynamic_module_type_lb_envoy_ptr, uint32_t)

WEAK_STUB(size_t, envoy_dynamic_module_callback_lb_get_healthy_hosts_count, 0,
          envoy_dynamic_module_type_lb_envoy_ptr, uint32_t)

WEAK_STUB(size_t, envoy_dynamic_module_callback_lb_get_degraded_hosts_count, 0,
          envoy_dynamic_module_type_lb_envoy_ptr, uint32_t)

WEAK_STUB(size_t, envoy_dynamic_module_callback_lb_get_priority_set_size, 0,
          envoy_dynamic_module_type_lb_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_lb_get_healthy_host_address, false,
          envoy_dynamic_module_type_lb_envoy_ptr, uint32_t, size_t,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(uint32_t, envoy_dynamic_module_callback_lb_get_healthy_host_weight, 0,
          envoy_dynamic_module_type_lb_envoy_ptr, uint32_t, size_t)

WEAK_STUB(envoy_dynamic_module_type_host_health, envoy_dynamic_module_callback_lb_get_host_health,
          envoy_dynamic_module_type_host_health_Unhealthy, envoy_dynamic_module_type_lb_envoy_ptr,
          uint32_t, size_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_lb_get_host_health_by_address, false,
          envoy_dynamic_module_type_lb_envoy_ptr, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_host_health*)

WEAK_STUB(bool, envoy_dynamic_module_callback_lb_get_host_address, false,
          envoy_dynamic_module_type_lb_envoy_ptr, uint32_t, size_t,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(uint32_t, envoy_dynamic_module_callback_lb_get_host_weight, 0,
          envoy_dynamic_module_type_lb_envoy_ptr, uint32_t, size_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_lb_get_host_locality, false,
          envoy_dynamic_module_type_lb_envoy_ptr, uint32_t, size_t,
          envoy_dynamic_module_type_envoy_buffer*, envoy_dynamic_module_type_envoy_buffer*,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_lb_context_compute_hash_key, false,
          envoy_dynamic_module_type_lb_context_envoy_ptr, uint64_t*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_lb_context_get_downstream_headers_size, 0,
          envoy_dynamic_module_type_lb_context_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_lb_context_get_downstream_headers, false,
          envoy_dynamic_module_type_lb_context_envoy_ptr,
          envoy_dynamic_module_type_envoy_http_header*)

WEAK_STUB(bool, envoy_dynamic_module_callback_lb_context_get_downstream_header, false,
          envoy_dynamic_module_type_lb_context_envoy_ptr, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_envoy_buffer*, size_t, size_t*)

WEAK_STUB(uint32_t, envoy_dynamic_module_callback_lb_context_get_host_selection_retry_count, 0,
          envoy_dynamic_module_type_lb_context_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_lb_context_should_select_another_host, false,
          envoy_dynamic_module_type_lb_envoy_ptr, envoy_dynamic_module_type_lb_context_envoy_ptr,
          uint32_t, size_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_lb_context_get_override_host, false,
          envoy_dynamic_module_type_lb_context_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*,
          bool*)

WEAK_STUB(bool, envoy_dynamic_module_callback_lb_set_host_data, false,
          envoy_dynamic_module_type_lb_envoy_ptr, uint32_t, size_t, uintptr_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_lb_get_host_data, false,
          envoy_dynamic_module_type_lb_envoy_ptr, uint32_t, size_t, uintptr_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_lb_get_host_metadata_string, false,
          envoy_dynamic_module_type_lb_envoy_ptr, uint32_t, size_t,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_lb_get_host_metadata_number, false,
          envoy_dynamic_module_type_lb_envoy_ptr, uint32_t, size_t,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer, double*)

WEAK_STUB(bool, envoy_dynamic_module_callback_lb_get_host_metadata_bool, false,
          envoy_dynamic_module_type_lb_envoy_ptr, uint32_t, size_t,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer, bool*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_lb_get_locality_count, 0,
          envoy_dynamic_module_type_lb_envoy_ptr, uint32_t)

WEAK_STUB(size_t, envoy_dynamic_module_callback_lb_get_locality_host_count, 0,
          envoy_dynamic_module_type_lb_envoy_ptr, uint32_t, size_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_lb_get_locality_host_address, false,
          envoy_dynamic_module_type_lb_envoy_ptr, uint32_t, size_t, size_t,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(uint32_t, envoy_dynamic_module_callback_lb_get_locality_weight, 0,
          envoy_dynamic_module_type_lb_envoy_ptr, uint32_t, size_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_lb_get_member_update_host_address, false,
          envoy_dynamic_module_type_lb_envoy_ptr, size_t, bool,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(uint64_t, envoy_dynamic_module_callback_lb_get_host_stat, 0,
          envoy_dynamic_module_type_lb_envoy_ptr, uint32_t, size_t,
          envoy_dynamic_module_type_host_stat)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_lb_config_define_counter,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_lb_config_envoy_ptr, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_module_buffer*, size_t, size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_lb_config_increment_counter,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_lb_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_lb_config_define_gauge,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_lb_config_envoy_ptr, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_module_buffer*, size_t, size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_lb_config_set_gauge,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_lb_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_lb_config_increment_gauge,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_lb_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_lb_config_decrement_gauge,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_lb_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_lb_config_define_histogram,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_lb_config_envoy_ptr, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_module_buffer*, size_t, size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_lb_config_record_histogram_value,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_lb_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

// ---------------------- Matcher callbacks ------------------------
// These are weak symbols that provide default stub implementations. The actual implementations
// are provided in the matcher extension abi_impl.cc when the matcher extension is used.

WEAK_STUB(size_t, envoy_dynamic_module_callback_matcher_get_headers_size, 0,
          envoy_dynamic_module_type_matcher_input_envoy_ptr,
          envoy_dynamic_module_type_http_header_type)

WEAK_STUB(bool, envoy_dynamic_module_callback_matcher_get_headers, false,
          envoy_dynamic_module_type_matcher_input_envoy_ptr,
          envoy_dynamic_module_type_http_header_type, envoy_dynamic_module_type_envoy_http_header*)

WEAK_STUB(bool, envoy_dynamic_module_callback_matcher_get_header_value, false,
          envoy_dynamic_module_type_matcher_input_envoy_ptr,
          envoy_dynamic_module_type_http_header_type, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_envoy_buffer*, size_t, size_t*)

WEAK_STUB_VOID(envoy_dynamic_module_callback_matcher_set_error,
               envoy_dynamic_module_type_matcher_input_envoy_ptr)

// ---------------------- Matcher data input callbacks ------------------------
// These are weak symbols that provide default stub implementations. The actual implementations
// are provided in the matcher data input extension abi_impl.cc when the extension is used.

WEAK_STUB(bool, envoy_dynamic_module_callback_matcher_data_input_get_header_value, false,
          envoy_dynamic_module_type_matcher_data_input_envoy_ptr,
          envoy_dynamic_module_type_http_header_type, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_envoy_buffer*, size_t, size_t*)

WEAK_STUB_VOID(envoy_dynamic_module_callback_matcher_data_input_set_result,
               envoy_dynamic_module_type_matcher_data_input_envoy_ptr,
               envoy_dynamic_module_type_module_buffer)

// ---------------------- Network filter callbacks ------------------------
// These are weak symbols that provide default stub implementations. The actual implementations
// are provided in the network filter abi_impl.cc when the network filter extension is used.

WEAK_STUB(size_t, envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks_size, 0,
          envoy_dynamic_module_type_network_filter_envoy_ptr)

WEAK_STUB(size_t, envoy_dynamic_module_callback_network_filter_get_read_buffer_size, 0,
          envoy_dynamic_module_type_network_filter_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks_size, 0,
          envoy_dynamic_module_type_network_filter_envoy_ptr)

WEAK_STUB(size_t, envoy_dynamic_module_callback_network_filter_get_write_buffer_size, 0,
          envoy_dynamic_module_type_network_filter_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_filter_drain_read_buffer, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr, size_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_filter_drain_write_buffer, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr, size_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_filter_prepend_read_buffer, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr,
          envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_filter_append_read_buffer, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr,
          envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_filter_prepend_write_buffer, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr,
          envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_filter_append_write_buffer, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr,
          envoy_dynamic_module_type_module_buffer)

WEAK_STUB_VOID(envoy_dynamic_module_callback_network_filter_write,
               envoy_dynamic_module_type_network_filter_envoy_ptr,
               envoy_dynamic_module_type_module_buffer, bool)

WEAK_STUB_VOID(envoy_dynamic_module_callback_network_filter_inject_read_data,
               envoy_dynamic_module_type_network_filter_envoy_ptr,
               envoy_dynamic_module_type_module_buffer, bool)

WEAK_STUB_VOID(envoy_dynamic_module_callback_network_filter_inject_write_data,
               envoy_dynamic_module_type_network_filter_envoy_ptr,
               envoy_dynamic_module_type_module_buffer, bool)

WEAK_STUB_VOID(envoy_dynamic_module_callback_network_filter_continue_reading,
               envoy_dynamic_module_type_network_filter_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_network_filter_close,
               envoy_dynamic_module_type_network_filter_envoy_ptr,
               envoy_dynamic_module_type_network_connection_close_type)

WEAK_STUB(uint64_t, envoy_dynamic_module_callback_network_filter_get_connection_id, 0,
          envoy_dynamic_module_type_network_filter_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_filter_get_remote_address, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*, uint32_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_filter_get_local_address, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*, uint32_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_filter_is_ssl, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_network_filter_disable_close,
               envoy_dynamic_module_type_network_filter_envoy_ptr, bool)

WEAK_STUB_VOID(envoy_dynamic_module_callback_network_filter_close_with_details,
               envoy_dynamic_module_type_network_filter_envoy_ptr,
               envoy_dynamic_module_type_network_connection_close_type,
               envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_filter_get_requested_server_name, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_filter_get_direct_remote_address, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*, uint32_t*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_network_filter_get_ssl_uri_sans_size, 0,
          envoy_dynamic_module_type_network_filter_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_filter_get_ssl_uri_sans, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_network_filter_get_ssl_dns_sans_size, 0,
          envoy_dynamic_module_type_network_filter_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_filter_get_ssl_dns_sans, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_filter_get_ssl_subject, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_set_filter_state_bytes, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_get_filter_state_bytes, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_set_filter_state_typed, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_get_filter_state_typed, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB_VOID(envoy_dynamic_module_callback_network_set_dynamic_metadata_string,
               envoy_dynamic_module_type_network_filter_envoy_ptr,
               envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer,
               envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_get_dynamic_metadata_string, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB_VOID(envoy_dynamic_module_callback_network_set_dynamic_metadata_number,
               envoy_dynamic_module_type_network_filter_envoy_ptr,
               envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer,
               double)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_get_dynamic_metadata_number, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer, double*)

WEAK_STUB_VOID(envoy_dynamic_module_callback_network_set_dynamic_metadata_bool,
               envoy_dynamic_module_type_network_filter_envoy_ptr,
               envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer,
               bool)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_get_dynamic_metadata_bool, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer, bool*)

WEAK_STUB_VOID(envoy_dynamic_module_callback_network_set_dynamic_metadata_string_batch,
               envoy_dynamic_module_type_network_filter_envoy_ptr,
               envoy_dynamic_module_type_module_buffer,
               const envoy_dynamic_module_type_module_key_value_pair*, size_t)

WEAK_STUB(envoy_dynamic_module_type_http_callout_init_result,
          envoy_dynamic_module_callback_network_filter_http_callout,
          envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest,
          envoy_dynamic_module_type_network_filter_envoy_ptr, uint64_t*,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_http_header*,
          size_t, envoy_dynamic_module_type_module_buffer, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_network_filter_config_define_counter,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_network_filter_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_network_filter_increment_counter,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_network_filter_envoy_ptr, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_network_filter_config_define_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_network_filter_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_network_filter_set_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_network_filter_envoy_ptr, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_network_filter_increment_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_network_filter_envoy_ptr, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_network_filter_decrement_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_network_filter_envoy_ptr, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_network_filter_config_define_histogram,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_network_filter_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_network_filter_record_histogram_value,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_network_filter_envoy_ptr, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_network_filter_config_increment_counter,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_network_filter_config_envoy_ptr, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_network_filter_config_increment_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_network_filter_config_envoy_ptr, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_network_filter_config_decrement_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_network_filter_config_envoy_ptr, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_network_filter_config_set_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_network_filter_config_envoy_ptr, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_network_filter_config_record_histogram_value,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_network_filter_config_envoy_ptr, size_t, uint64_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_filter_get_cluster_host_count, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, uint32_t, size_t*, size_t*, size_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_filter_get_upstream_host_address, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*, uint32_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_filter_get_upstream_host_hostname, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_filter_get_upstream_host_cluster, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_filter_has_upstream_host, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr)

WEAK_STUB(uint64_t, envoy_dynamic_module_callback_network_filter_get_upstream_connection_id, 0,
          envoy_dynamic_module_type_network_filter_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_filter_start_downstream_secure_transport,
          false, envoy_dynamic_module_type_network_filter_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_filter_start_upstream_secure_transport, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr)

WEAK_STUB(envoy_dynamic_module_type_network_connection_state,
          envoy_dynamic_module_callback_network_filter_get_connection_state,
          envoy_dynamic_module_type_network_connection_state_Closed,
          envoy_dynamic_module_type_network_filter_envoy_ptr)

WEAK_STUB(envoy_dynamic_module_type_network_read_disable_status,
          envoy_dynamic_module_callback_network_filter_read_disable,
          envoy_dynamic_module_type_network_read_disable_status_NoTransition,
          envoy_dynamic_module_type_network_filter_envoy_ptr, bool)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_filter_read_enabled, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_filter_is_half_close_enabled, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_network_filter_enable_half_close,
               envoy_dynamic_module_type_network_filter_envoy_ptr, bool)

WEAK_STUB(uint32_t, envoy_dynamic_module_callback_network_filter_get_buffer_limit, 0,
          envoy_dynamic_module_type_network_filter_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_network_filter_set_buffer_limits,
               envoy_dynamic_module_type_network_filter_envoy_ptr, uint32_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_filter_above_high_watermark, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr)

WEAK_STUB(envoy_dynamic_module_type_network_filter_scheduler_module_ptr,
          envoy_dynamic_module_callback_network_filter_scheduler_new, nullptr,
          envoy_dynamic_module_type_network_filter_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_network_filter_scheduler_commit,
               envoy_dynamic_module_type_network_filter_scheduler_module_ptr, uint64_t)

WEAK_STUB_VOID(envoy_dynamic_module_callback_network_filter_scheduler_delete,
               envoy_dynamic_module_type_network_filter_scheduler_module_ptr)

WEAK_STUB(envoy_dynamic_module_type_network_filter_config_scheduler_module_ptr,
          envoy_dynamic_module_callback_network_filter_config_scheduler_new, nullptr,
          envoy_dynamic_module_type_network_filter_config_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_network_filter_config_scheduler_delete,
               envoy_dynamic_module_type_network_filter_config_scheduler_module_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_network_filter_config_scheduler_commit,
               envoy_dynamic_module_type_network_filter_config_scheduler_module_ptr, uint64_t)

WEAK_STUB(uint32_t, envoy_dynamic_module_callback_network_filter_get_worker_index, 0,
          envoy_dynamic_module_type_network_filter_envoy_ptr)

// ---------------------- Socket Option Callbacks (Network) --------------------

WEAK_STUB_VOID(envoy_dynamic_module_callback_network_set_socket_option_int,
               envoy_dynamic_module_type_network_filter_envoy_ptr, int64_t, int64_t,
               envoy_dynamic_module_type_socket_option_state, int64_t)

WEAK_STUB_VOID(envoy_dynamic_module_callback_network_set_socket_option_bytes,
               envoy_dynamic_module_type_network_filter_envoy_ptr, int64_t, int64_t,
               envoy_dynamic_module_type_socket_option_state,
               envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_get_socket_option_int, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr, int64_t, int64_t,
          envoy_dynamic_module_type_socket_option_state, int64_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_get_socket_option_bytes, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr, int64_t, int64_t,
          envoy_dynamic_module_type_socket_option_state, envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_network_get_socket_options_size, 0,
          envoy_dynamic_module_type_network_filter_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_network_get_socket_options,
               envoy_dynamic_module_type_network_filter_envoy_ptr,
               envoy_dynamic_module_type_socket_option*)

// ---------------------- Listener Filter Callbacks ----------------------------

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_get_buffer_chunk, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_drain_buffer, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr, size_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_get_remote_address, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*, uint32_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_get_direct_remote_address, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*, uint32_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_get_local_address, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*, uint32_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_get_direct_local_address, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*, uint32_t*)

WEAK_STUB(envoy_dynamic_module_type_listener_filter_scheduler_module_ptr,
          envoy_dynamic_module_callback_listener_filter_scheduler_new, nullptr,
          envoy_dynamic_module_type_listener_filter_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_listener_filter_scheduler_commit,
               envoy_dynamic_module_type_listener_filter_scheduler_module_ptr, uint64_t)

WEAK_STUB_VOID(envoy_dynamic_module_callback_listener_filter_scheduler_delete,
               envoy_dynamic_module_type_listener_filter_scheduler_module_ptr)

WEAK_STUB(envoy_dynamic_module_type_listener_filter_config_scheduler_module_ptr,
          envoy_dynamic_module_callback_listener_filter_config_scheduler_new, nullptr,
          envoy_dynamic_module_type_listener_filter_config_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_listener_filter_config_scheduler_delete,
               envoy_dynamic_module_type_listener_filter_config_scheduler_module_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_listener_filter_config_scheduler_commit,
               envoy_dynamic_module_type_listener_filter_config_scheduler_module_ptr, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_listener_filter_config_define_counter,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_listener_filter_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_listener_filter_config_define_gauge,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_listener_filter_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_listener_filter_config_define_histogram,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_listener_filter_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, size_t*)

// ============================================================
// Auto-generated weak stubs for filter types not compiled in
// ============================================================

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_access_logger_config_define_counter,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_access_logger_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_access_logger_config_define_gauge,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_access_logger_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_access_logger_config_define_histogram,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_access_logger_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_access_logger_decrement_gauge,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_access_logger_config_envoy_ptr, size_t, uint64_t)

WEAK_STUB(uint32_t, envoy_dynamic_module_callback_access_logger_get_attempt_count, 0,
          envoy_dynamic_module_type_access_logger_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_attribute_bool, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_attribute_id,
          bool*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_attribute_int, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_attribute_id,
          uint64_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_attribute_string, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_attribute_id,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB_VOID(envoy_dynamic_module_callback_access_logger_get_bytes_info,
               envoy_dynamic_module_type_access_logger_envoy_ptr,
               envoy_dynamic_module_type_bytes_info*)

WEAK_STUB_VOID(envoy_dynamic_module_callback_access_logger_get_downstream_wire_bytes,
               envoy_dynamic_module_type_access_logger_envoy_ptr,
               envoy_dynamic_module_type_downstream_wire_bytes*)

WEAK_STUB(uint64_t, envoy_dynamic_module_callback_access_logger_get_connection_id, 0,
          envoy_dynamic_module_type_access_logger_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_connection_termination_details,
          false, envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_downstream_direct_local_address,
          false, envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*, uint32_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_downstream_direct_remote_address,
          false, envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*, uint32_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_downstream_local_address, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*, uint32_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_downstream_local_dns_san, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_access_logger_get_downstream_local_dns_san_size, 0,
          envoy_dynamic_module_type_access_logger_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_downstream_local_subject, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_downstream_local_uri_san, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_access_logger_get_downstream_local_uri_san_size, 0,
          envoy_dynamic_module_type_access_logger_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_digest, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_presented,
          false, envoy_dynamic_module_type_access_logger_envoy_ptr)

WEAK_STUB(int64_t, envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_v_end, 0,
          envoy_dynamic_module_type_access_logger_envoy_ptr)

WEAK_STUB(int64_t, envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_v_start, 0,
          envoy_dynamic_module_type_access_logger_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_validated,
          false, envoy_dynamic_module_type_access_logger_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_downstream_peer_dns_san, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_access_logger_get_downstream_peer_dns_san_size, 0,
          envoy_dynamic_module_type_access_logger_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_downstream_peer_fingerprint_1,
          false, envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_downstream_peer_issuer, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_downstream_peer_serial, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_downstream_peer_subject, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_downstream_peer_uri_san, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_access_logger_get_downstream_peer_uri_san_size, 0,
          envoy_dynamic_module_type_access_logger_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_downstream_remote_address, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*, uint32_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_downstream_tls_cipher, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_downstream_tls_session_id, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_downstream_tls_version, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_downstream_transport_failure_reason,
          false, envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_dynamic_metadata, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_dynamic_metadata_number, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer, double*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_dynamic_metadata_bool, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer, bool*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_filter_state, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_header_value, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_http_header_type, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_envoy_buffer*, size_t, size_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_headers, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_http_header_type, envoy_dynamic_module_type_envoy_http_header*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_access_logger_get_headers_size, 0,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_http_header_type)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_ja3_hash, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_ja4_hash, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_local_reply_body, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_protocol, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(uint64_t, envoy_dynamic_module_callback_access_logger_get_request_headers_bytes, 0,
          envoy_dynamic_module_type_access_logger_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_request_id, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_requested_server_name, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(uint32_t, envoy_dynamic_module_callback_access_logger_get_response_code, 0,
          envoy_dynamic_module_type_access_logger_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_response_code_details, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(uint64_t, envoy_dynamic_module_callback_access_logger_get_response_flags, 0,
          envoy_dynamic_module_type_access_logger_envoy_ptr)

WEAK_STUB(uint64_t, envoy_dynamic_module_callback_access_logger_get_response_headers_bytes, 0,
          envoy_dynamic_module_type_access_logger_envoy_ptr)

WEAK_STUB(uint64_t, envoy_dynamic_module_callback_access_logger_get_response_trailers_bytes, 0,
          envoy_dynamic_module_type_access_logger_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_route_name, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_span_id, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB_VOID(envoy_dynamic_module_callback_access_logger_get_timing_info,
               envoy_dynamic_module_type_access_logger_envoy_ptr,
               envoy_dynamic_module_type_timing_info*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_trace_id, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_upstream_cluster, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(uint64_t, envoy_dynamic_module_callback_access_logger_get_upstream_connection_id, 0,
          envoy_dynamic_module_type_access_logger_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_upstream_host, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_upstream_local_address, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*, uint32_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_upstream_local_dns_san, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_access_logger_get_upstream_local_dns_san_size, 0,
          envoy_dynamic_module_type_access_logger_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_upstream_local_subject, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_upstream_local_uri_san, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_access_logger_get_upstream_local_uri_san_size, 0,
          envoy_dynamic_module_type_access_logger_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_upstream_peer_cert_digest, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(int64_t, envoy_dynamic_module_callback_access_logger_get_upstream_peer_cert_v_end, 0,
          envoy_dynamic_module_type_access_logger_envoy_ptr)

WEAK_STUB(int64_t, envoy_dynamic_module_callback_access_logger_get_upstream_peer_cert_v_start, 0,
          envoy_dynamic_module_type_access_logger_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_upstream_peer_dns_san, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_access_logger_get_upstream_peer_dns_san_size, 0,
          envoy_dynamic_module_type_access_logger_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_upstream_peer_issuer, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_upstream_peer_subject, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_upstream_peer_uri_san, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_access_logger_get_upstream_peer_uri_san_size, 0,
          envoy_dynamic_module_type_access_logger_envoy_ptr)

WEAK_STUB(int64_t, envoy_dynamic_module_callback_access_logger_get_upstream_pool_ready_duration_ns,
          0, envoy_dynamic_module_type_access_logger_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_upstream_protocol, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_upstream_remote_address, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*, uint32_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_upstream_tls_cipher, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_upstream_tls_session_id, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_upstream_tls_version, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_upstream_transport_failure_reason,
          false, envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_get_virtual_cluster_name, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(uint32_t, envoy_dynamic_module_callback_access_logger_get_worker_index, 0,
          envoy_dynamic_module_type_access_logger_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_has_response_flag, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr,
          envoy_dynamic_module_type_response_flag)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_access_logger_increment_counter,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_access_logger_config_envoy_ptr, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_access_logger_increment_gauge,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_access_logger_config_envoy_ptr, size_t, uint64_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_is_health_check, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_is_mtls, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_access_logger_is_trace_sampled, false,
          envoy_dynamic_module_type_access_logger_envoy_ptr)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_access_logger_record_histogram_value,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_access_logger_config_envoy_ptr, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_access_logger_set_gauge,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_access_logger_config_envoy_ptr, size_t, uint64_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_specifier_get_attribute_bool, false,
          envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr,
          envoy_dynamic_module_type_attribute_id, bool*)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_specifier_get_attribute_int, false,
          envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr,
          envoy_dynamic_module_type_attribute_id, uint64_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_specifier_get_attribute_string, false,
          envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr,
          envoy_dynamic_module_type_attribute_id, envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_specifier_get_dynamic_metadata, false,
          envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_specifier_get_dynamic_metadata_number, false,
          envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer, double*)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_specifier_get_dynamic_metadata_bool, false,
          envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer, bool*)

WEAK_STUB(uint64_t, envoy_dynamic_module_callback_cluster_specifier_get_random_value, 0,
          envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_specifier_get_cluster_host_count, false,
          envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, uint32_t, size_t*, size_t*, size_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_specifier_get_request_header_value, false,
          envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_envoy_buffer*, size_t,
          size_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_specifier_get_request_headers, false,
          envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr,
          envoy_dynamic_module_type_envoy_http_header*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_cluster_specifier_get_request_headers_size, 0,
          envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_specifier_get_route_name, false,
          envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB_VOID(envoy_dynamic_module_callback_cluster_specifier_set_cluster_name,
               envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr,
               envoy_dynamic_module_type_module_buffer)

WEAK_STUB_VOID(envoy_dynamic_module_callback_cluster_specifier_set_idle_timeout,
               envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr, uint64_t)

WEAK_STUB_VOID(envoy_dynamic_module_callback_cluster_specifier_set_priority,
               envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr,
               envoy_dynamic_module_type_resource_priority)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_specifier_set_cluster_not_found_response_code,
          false, envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr, uint32_t)

WEAK_STUB_VOID(envoy_dynamic_module_callback_cluster_specifier_set_request_body_buffer_limit,
               envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr, uint64_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_cluster_specifier_set_route_action_override, false,
          envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr,
          envoy_dynamic_module_type_module_buffer)

WEAK_STUB_VOID(envoy_dynamic_module_callback_cluster_specifier_set_timeout,
               envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr, uint64_t)

WEAK_STUB_VOID(envoy_dynamic_module_callback_cluster_specifier_set_route_metadata_number,
               envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr,
               envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer,
               double)

WEAK_STUB_VOID(envoy_dynamic_module_callback_cluster_specifier_set_route_metadata_string,
               envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr,
               envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer,
               envoy_dynamic_module_type_module_buffer)

WEAK_STUB_VOID(envoy_dynamic_module_callback_cluster_specifier_set_route_metadata_bool,
               envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr,
               envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer,
               bool)

WEAK_STUB_VOID(envoy_dynamic_module_callback_cluster_specifier_set_route_metadata_struct,
               envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr,
               envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer)

WEAK_STUB_VOID(envoy_dynamic_module_callback_cluster_specifier_set_route_typed_metadata,
               envoy_dynamic_module_type_cluster_specifier_context_envoy_ptr,
               envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_cluster_specifier_config_define_counter,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_cluster_specifier_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer*, size_t,
          size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_cluster_specifier_config_increment_counter,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_cluster_specifier_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_cluster_specifier_config_define_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_cluster_specifier_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer*, size_t,
          size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_cluster_specifier_config_set_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_cluster_specifier_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_cluster_specifier_config_increment_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_cluster_specifier_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_cluster_specifier_config_decrement_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_cluster_specifier_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_cluster_specifier_config_define_histogram,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_cluster_specifier_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer*, size_t,
          size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_cluster_specifier_config_record_histogram_value,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_cluster_specifier_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_early_header_mutation_add_header, false,
          envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_early_header_mutation_get_attribute_bool, false,
          envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr,
          envoy_dynamic_module_type_attribute_id, bool*)

WEAK_STUB(bool, envoy_dynamic_module_callback_early_header_mutation_get_attribute_int, false,
          envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr,
          envoy_dynamic_module_type_attribute_id, uint64_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_early_header_mutation_get_attribute_string, false,
          envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr,
          envoy_dynamic_module_type_attribute_id, envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_early_header_mutation_get_dynamic_metadata, false,
          envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_early_header_mutation_get_dynamic_metadata_bool,
          false, envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer, bool*)

WEAK_STUB(bool, envoy_dynamic_module_callback_early_header_mutation_get_dynamic_metadata_number,
          false, envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer, double*)

WEAK_STUB(bool, envoy_dynamic_module_callback_early_header_mutation_get_filter_state_bytes, false,
          envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_early_header_mutation_get_header_value, false,
          envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_envoy_buffer*, size_t,
          size_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_early_header_mutation_get_headers, false,
          envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr,
          envoy_dynamic_module_type_envoy_http_header*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_early_header_mutation_get_headers_size, 0,
          envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_early_header_mutation_remove_header, false,
          envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr,
          envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_early_header_mutation_set_header, false,
          envoy_dynamic_module_type_early_header_mutation_context_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer)

WEAK_STUB(envoy_dynamic_module_type_access_log_type,
          envoy_dynamic_module_callback_formatter_get_access_log_type,
          envoy_dynamic_module_type_access_log_type_NotSet,
          envoy_dynamic_module_type_formatter_context_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_formatter_get_attribute_bool, false,
          envoy_dynamic_module_type_formatter_context_envoy_ptr,
          envoy_dynamic_module_type_attribute_id, bool*)

WEAK_STUB(bool, envoy_dynamic_module_callback_formatter_get_attribute_int, false,
          envoy_dynamic_module_type_formatter_context_envoy_ptr,
          envoy_dynamic_module_type_attribute_id, uint64_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_formatter_get_attribute_string, false,
          envoy_dynamic_module_type_formatter_context_envoy_ptr,
          envoy_dynamic_module_type_attribute_id, envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_formatter_get_dynamic_metadata, false,
          envoy_dynamic_module_type_formatter_context_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_formatter_get_header_value, false,
          envoy_dynamic_module_type_formatter_context_envoy_ptr,
          envoy_dynamic_module_type_http_header_type, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_envoy_buffer*, size_t, size_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_formatter_get_headers, false,
          envoy_dynamic_module_type_formatter_context_envoy_ptr,
          envoy_dynamic_module_type_http_header_type, envoy_dynamic_module_type_envoy_http_header*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_formatter_get_headers_size, 0,
          envoy_dynamic_module_type_formatter_context_envoy_ptr,
          envoy_dynamic_module_type_http_header_type)

WEAK_STUB(bool, envoy_dynamic_module_callback_formatter_get_local_reply_body, false,
          envoy_dynamic_module_type_formatter_context_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB_VOID(envoy_dynamic_module_callback_listener_filter_close_socket,
               envoy_dynamic_module_type_listener_filter_envoy_ptr,
               envoy_dynamic_module_type_module_buffer)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_listener_filter_decrement_gauge,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_listener_filter_envoy_ptr, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_address_type,
          envoy_dynamic_module_callback_listener_filter_get_address_type,
          envoy_dynamic_module_type_address_type_Unknown,
          envoy_dynamic_module_type_listener_filter_envoy_ptr)

WEAK_STUB(uint64_t, envoy_dynamic_module_callback_listener_filter_get_connection_start_time_ms, 0,
          envoy_dynamic_module_type_listener_filter_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_get_detected_transport_protocol,
          false, envoy_dynamic_module_type_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_get_dynamic_metadata_string, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_get_ja3_hash, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_get_ja4_hash, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_get_original_dst, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*, uint32_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_get_requested_application_protocols,
          false, envoy_dynamic_module_type_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(size_t,
          envoy_dynamic_module_callback_listener_filter_get_requested_application_protocols_size, 0,
          envoy_dynamic_module_type_listener_filter_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_get_requested_server_name, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(int64_t, envoy_dynamic_module_callback_listener_filter_get_socket_fd, 0,
          envoy_dynamic_module_type_listener_filter_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_get_socket_option_bytes, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr, int64_t, int64_t, char*, size_t,
          size_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_get_socket_option_int, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr, int64_t, int64_t, int64_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_get_ssl_dns_sans, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_listener_filter_get_ssl_dns_sans_size, 0,
          envoy_dynamic_module_type_listener_filter_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_get_ssl_subject, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_get_ssl_uri_sans, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_listener_filter_get_ssl_uri_sans_size, 0,
          envoy_dynamic_module_type_listener_filter_envoy_ptr)

WEAK_STUB(uint32_t, envoy_dynamic_module_callback_listener_filter_get_worker_index, 0,
          envoy_dynamic_module_type_listener_filter_envoy_ptr)

WEAK_STUB(envoy_dynamic_module_type_http_callout_init_result,
          envoy_dynamic_module_callback_listener_filter_http_callout,
          envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest,
          envoy_dynamic_module_type_listener_filter_envoy_ptr, uint64_t*,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_http_header*,
          size_t, envoy_dynamic_module_type_module_buffer, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_listener_filter_increment_counter,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_listener_filter_envoy_ptr, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_listener_filter_increment_gauge,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_listener_filter_envoy_ptr, size_t, uint64_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_is_local_address_restored, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_is_ssl, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr)

WEAK_STUB(size_t, envoy_dynamic_module_callback_listener_filter_max_read_bytes, 0,
          envoy_dynamic_module_type_listener_filter_envoy_ptr)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_listener_filter_record_histogram_value,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_listener_filter_envoy_ptr, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_listener_filter_config_increment_counter,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_listener_filter_config_envoy_ptr, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_listener_filter_config_increment_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_listener_filter_config_envoy_ptr, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_listener_filter_config_decrement_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_listener_filter_config_envoy_ptr, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_listener_filter_config_set_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_listener_filter_config_envoy_ptr, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_listener_filter_config_record_histogram_value,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_listener_filter_config_envoy_ptr, size_t, uint64_t)

WEAK_STUB_VOID(
    envoy_dynamic_module_callback_listener_filter_set_downstream_transport_failure_reason,
    envoy_dynamic_module_type_listener_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer)

WEAK_STUB_VOID(envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata_string,
               envoy_dynamic_module_type_listener_filter_envoy_ptr,
               envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer,
               envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_get_dynamic_metadata_number, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer, double*)

WEAK_STUB_VOID(envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata_number,
               envoy_dynamic_module_type_listener_filter_envoy_ptr,
               envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer,
               double)

WEAK_STUB_VOID(envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata_string_batch,
               envoy_dynamic_module_type_listener_filter_envoy_ptr,
               envoy_dynamic_module_type_module_buffer,
               const envoy_dynamic_module_type_module_key_value_pair*, size_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_listener_filter_set_gauge,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_listener_filter_envoy_ptr, size_t, uint64_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_set_socket_option_bytes, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr, int64_t, int64_t,
          envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_set_socket_option_int, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr, int64_t, int64_t, int64_t)

WEAK_STUB_VOID(envoy_dynamic_module_callback_listener_filter_use_original_dst,
               envoy_dynamic_module_type_listener_filter_envoy_ptr, bool)

WEAK_STUB(int64_t, envoy_dynamic_module_callback_listener_filter_write_to_socket, 0,
          envoy_dynamic_module_type_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_module_buffer)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_udp_listener_filter_config_define_counter,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_udp_listener_filter_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_udp_listener_filter_config_define_gauge,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_udp_listener_filter_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_udp_listener_filter_config_define_histogram,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_udp_listener_filter_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_udp_listener_filter_decrement_gauge,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_udp_listener_filter_envoy_ptr, size_t, uint64_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks, false,
          envoy_dynamic_module_type_udp_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks_size,
          0, envoy_dynamic_module_type_udp_listener_filter_envoy_ptr)

WEAK_STUB(size_t, envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_size, 0,
          envoy_dynamic_module_type_udp_listener_filter_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_udp_listener_filter_get_local_address, false,
          envoy_dynamic_module_type_udp_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*, uint32_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_udp_listener_filter_get_peer_address, false,
          envoy_dynamic_module_type_udp_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*, uint32_t*)

WEAK_STUB(uint32_t, envoy_dynamic_module_callback_udp_listener_filter_get_worker_index, 0,
          envoy_dynamic_module_type_udp_listener_filter_envoy_ptr)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_udp_listener_filter_increment_counter,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_udp_listener_filter_envoy_ptr, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_udp_listener_filter_increment_gauge,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_udp_listener_filter_envoy_ptr, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_udp_listener_filter_record_histogram_value,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_udp_listener_filter_envoy_ptr, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_udp_listener_filter_config_increment_counter,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_udp_listener_filter_config_envoy_ptr, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_udp_listener_filter_config_increment_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_udp_listener_filter_config_envoy_ptr, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_udp_listener_filter_config_decrement_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_udp_listener_filter_config_envoy_ptr, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_udp_listener_filter_config_set_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_udp_listener_filter_config_envoy_ptr, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_udp_listener_filter_config_record_histogram_value,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_udp_listener_filter_config_envoy_ptr, size_t, uint64_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_udp_listener_filter_send_datagram, false,
          envoy_dynamic_module_type_udp_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer,
          uint32_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_udp_listener_filter_set_datagram_data, false,
          envoy_dynamic_module_type_udp_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_module_buffer)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_udp_listener_filter_set_gauge,
          envoy_dynamic_module_type_metrics_result_Success,
          envoy_dynamic_module_type_udp_listener_filter_envoy_ptr, size_t, uint64_t)

// ---------------------- Upstream HTTP TCP Bridge callbacks ------------------------
// These are weak symbols that provide default stub implementations. The actual implementations
// are provided in the upstream bridge abi_impl.cc when the upstream bridge extension is used.

WEAK_STUB(bool, envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_header, false,
          envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_envoy_buffer*, size_t,
          size_t*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_headers_size,
          0, envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_headers, false,
          envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
          envoy_dynamic_module_type_envoy_http_header*)

WEAK_STUB_VOID(envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_buffer,
               envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
               envoy_dynamic_module_type_envoy_buffer*, size_t*)

WEAK_STUB(size_t,
          envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_buffer_chunks_size, 0,
          envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_response_buffer,
               envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
               envoy_dynamic_module_type_envoy_buffer*, size_t*)

WEAK_STUB(size_t,
          envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_response_buffer_chunks_size, 0,
          envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_upstream_data,
               envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
               envoy_dynamic_module_type_module_buffer, bool)

WEAK_STUB_VOID(envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response,
               envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr, uint32_t,
               envoy_dynamic_module_type_module_http_header*, size_t,
               envoy_dynamic_module_type_module_buffer)

WEAK_STUB_VOID(envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_headers,
               envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr, uint32_t,
               envoy_dynamic_module_type_module_http_header*, size_t, bool)

WEAK_STUB_VOID(envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_data,
               envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
               envoy_dynamic_module_type_module_buffer, bool)

WEAK_STUB_VOID(envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_trailers,
               envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
               envoy_dynamic_module_type_module_http_header*, size_t)

// ---------------------- Tracer callbacks ------------------------
// These are weak symbols that provide default stub implementations. The actual implementations
// are provided in the tracer abi_impl.cc when the tracer extension is used.

WEAK_STUB(bool, envoy_dynamic_module_callback_tracer_get_trace_context_value, false,
          envoy_dynamic_module_type_tracer_span_envoy_ptr, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB_VOID(envoy_dynamic_module_callback_tracer_set_trace_context_value,
               envoy_dynamic_module_type_tracer_span_envoy_ptr,
               envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer)

WEAK_STUB_VOID(envoy_dynamic_module_callback_tracer_remove_trace_context_value,
               envoy_dynamic_module_type_tracer_span_envoy_ptr,
               envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_tracer_get_trace_context_protocol, false,
          envoy_dynamic_module_type_tracer_span_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_tracer_get_trace_context_host, false,
          envoy_dynamic_module_type_tracer_span_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_tracer_get_trace_context_path, false,
          envoy_dynamic_module_type_tracer_span_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_tracer_get_trace_context_method, false,
          envoy_dynamic_module_type_tracer_span_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_tracer_define_counter,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_tracer_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer*, size_t,
          size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_tracer_define_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_tracer_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer*, size_t,
          size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_tracer_define_histogram,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_tracer_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer*, size_t,
          size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_tracer_increment_counter,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_tracer_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_tracer_record_histogram_value,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_tracer_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result, envoy_dynamic_module_callback_tracer_set_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_tracer_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_add_dynamic_metadata_list_number, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_module_buffer, double)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_add_dynamic_metadata_list_string, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_add_dynamic_metadata_list_bool, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_module_buffer, bool)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_get_metadata_list_size, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr,
          envoy_dynamic_module_type_metadata_source, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_module_buffer, size_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_get_metadata_list_number, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr,
          envoy_dynamic_module_type_metadata_source, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_module_buffer, size_t, double*)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_get_metadata_list_string, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr,
          envoy_dynamic_module_type_metadata_source, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_module_buffer, size_t, envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_get_metadata_list_bool, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr,
          envoy_dynamic_module_type_metadata_source, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_module_buffer, size_t, bool*)

// DNS resolver callbacks.
WEAK_STUB_VOID(envoy_dynamic_module_callback_dns_resolve_complete,
               envoy_dynamic_module_type_dns_resolver_envoy_ptr, uint64_t,
               envoy_dynamic_module_type_dns_resolution_status,
               envoy_dynamic_module_type_module_buffer,
               const envoy_dynamic_module_type_dns_address*, size_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_dns_resolver_config_define_counter,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_dns_resolver_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer*, size_t,
          size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_dns_resolver_config_increment_counter,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_dns_resolver_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_dns_resolver_config_define_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_dns_resolver_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer*, size_t,
          size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_dns_resolver_config_set_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_dns_resolver_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_dns_resolver_config_increment_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_dns_resolver_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_dns_resolver_config_decrement_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_dns_resolver_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_dns_resolver_config_define_histogram,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_dns_resolver_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer*, size_t,
          size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_dns_resolver_config_record_histogram_value,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_dns_resolver_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

// Transport socket callbacks.
WEAK_STUB(envoy_dynamic_module_type_transport_socket_io_status,
          envoy_dynamic_module_callback_transport_socket_io_read,
          envoy_dynamic_module_type_transport_socket_io_status_Error,
          envoy_dynamic_module_type_transport_socket_envoy_ptr, char*, size_t, size_t*)

WEAK_STUB(envoy_dynamic_module_type_transport_socket_io_status,
          envoy_dynamic_module_callback_transport_socket_io_write,
          envoy_dynamic_module_type_transport_socket_io_status_Error,
          envoy_dynamic_module_type_transport_socket_envoy_ptr, const char*, size_t, size_t*)

WEAK_STUB_VOID(envoy_dynamic_module_callback_transport_socket_io_shutdown_write,
               envoy_dynamic_module_type_transport_socket_envoy_ptr)

WEAK_STUB(int, envoy_dynamic_module_callback_transport_socket_get_fd, -1,
          envoy_dynamic_module_type_transport_socket_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_transport_socket_read_buffer_add,
               envoy_dynamic_module_type_transport_socket_envoy_ptr, const char*, size_t)

WEAK_STUB_VOID(envoy_dynamic_module_callback_transport_socket_write_buffer_drain,
               envoy_dynamic_module_type_transport_socket_envoy_ptr, size_t)

WEAK_STUB_VOID(envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices,
               envoy_dynamic_module_type_transport_socket_envoy_ptr,
               envoy_dynamic_module_type_envoy_buffer*, size_t*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_transport_socket_write_buffer_length, 0,
          envoy_dynamic_module_type_transport_socket_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_transport_socket_raise_event,
               envoy_dynamic_module_type_transport_socket_envoy_ptr,
               envoy_dynamic_module_type_network_connection_event)

WEAK_STUB(bool, envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer, false,
          envoy_dynamic_module_type_transport_socket_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_transport_socket_set_is_readable,
               envoy_dynamic_module_type_transport_socket_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_transport_socket_set_is_writable,
               envoy_dynamic_module_type_transport_socket_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_transport_socket_flush_write_buffer,
               envoy_dynamic_module_type_transport_socket_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_transport_socket_get_remote_address, false,
          envoy_dynamic_module_type_transport_socket_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*, uint32_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_transport_socket_get_local_address, false,
          envoy_dynamic_module_type_transport_socket_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*, uint32_t*)

// Stats sink snapshot callbacks. Real implementations live in
// source/extensions/stat_sinks/dynamic_modules/abi_impl.cc when the stats sink extension is linked.

WEAK_STUB(size_t, envoy_dynamic_module_callback_stat_sink_snapshot_get_counter_count, 0,
          envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_stat_sink_snapshot_get_counter, false,
          envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr, size_t, char*, size_t, size_t*,
          uint64_t*, uint64_t*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge_count, 0,
          envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge, false,
          envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr, size_t, char*, size_t, size_t*,
          uint64_t*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout_count, 0,
          envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout, false,
          envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr, size_t, char*, size_t, size_t*,
          char*, size_t, size_t*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_stat_sink_snapshot_get_histogram_count, 0,
          envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_stat_sink_snapshot_get_histogram, false,
          envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr, size_t, char*, size_t, size_t*,
          uint64_t*, double*)

WEAK_STUB(bool, envoy_dynamic_module_callback_stat_sink_snapshot_get_counter_tag_extracted_name,
          false, envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr, size_t, char*, size_t,
          size_t*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_stat_sink_snapshot_get_histogram_bucket_count, 0,
          envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr, size_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_stat_sink_snapshot_get_histogram_bucket, false,
          envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr, size_t, size_t, double*,
          uint64_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_stat_sink_snapshot_get_histogram_tag_extracted_name,
          false, envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr, size_t, char*, size_t,
          size_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_stat_sink_snapshot_get_histogram_tag_count, false,
          envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr, size_t, size_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_stat_sink_snapshot_get_histogram_tag, false,
          envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr, size_t, size_t, char*, size_t,
          size_t*, char*, size_t, size_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_stat_sink_snapshot_get_counter_tag_count, false,
          envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr, size_t, size_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_stat_sink_snapshot_get_counter_tag, false,
          envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr, size_t, size_t, char*, size_t,
          size_t*, char*, size_t, size_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge_tag_extracted_name,
          false, envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr, size_t, char*, size_t,
          size_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge_tag_count, false,
          envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr, size_t, size_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge_tag, false,
          envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr, size_t, size_t, char*, size_t,
          size_t*, char*, size_t, size_t*)

WEAK_STUB(bool,
          envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout_tag_extracted_name,
          false, envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr, size_t, char*, size_t,
          size_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout_tag_count, false,
          envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr, size_t, size_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout_tag, false,
          envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr, size_t, size_t, char*, size_t,
          size_t*, char*, size_t, size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_stat_sink_config_define_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_stat_sink_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_stat_sink_config_set_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_stat_sink_config_envoy_ptr, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_stat_sink_config_scheduler_module_ptr,
          envoy_dynamic_module_callback_stat_sink_config_scheduler_new, nullptr,
          envoy_dynamic_module_type_stat_sink_config_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_stat_sink_config_scheduler_commit,
               envoy_dynamic_module_type_stat_sink_config_scheduler_module_ptr, uint64_t)

WEAK_STUB_VOID(envoy_dynamic_module_callback_stat_sink_config_scheduler_delete,
               envoy_dynamic_module_type_stat_sink_config_scheduler_module_ptr)

// Additional weak stubs for callbacks declared in abi.h but not implemented above.

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_http_filter_config_define_counter,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_http_filter_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer*, size_t,
          size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_http_filter_increment_counter,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_http_filter_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_http_filter_config_define_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_http_filter_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer*, size_t,
          size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_http_filter_increment_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_http_filter_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_http_filter_decrement_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_http_filter_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_http_filter_set_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_http_filter_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_http_filter_config_define_histogram,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_http_filter_config_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer*, size_t,
          size_t*)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_http_filter_record_histogram_value,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_http_filter_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_http_filter_config_increment_counter,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_http_filter_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_http_filter_config_increment_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_http_filter_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_http_filter_config_decrement_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_http_filter_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_http_filter_config_set_gauge,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_http_filter_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_metrics_result,
          envoy_dynamic_module_callback_http_filter_config_record_histogram_value,
          envoy_dynamic_module_type_metrics_result_MetricNotFound,
          envoy_dynamic_module_type_http_filter_config_envoy_ptr, size_t,
          envoy_dynamic_module_type_module_buffer*, size_t, uint64_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_get_header, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr,
          envoy_dynamic_module_type_http_header_type, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_envoy_buffer*, size_t, size_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_get_header_values, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr,
          envoy_dynamic_module_type_http_header_type, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_http_get_headers_size, 0,
          envoy_dynamic_module_type_http_filter_envoy_ptr,
          envoy_dynamic_module_type_http_header_type)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_get_headers, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr,
          envoy_dynamic_module_type_http_header_type, envoy_dynamic_module_type_envoy_http_header*)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_add_header, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr,
          envoy_dynamic_module_type_http_header_type, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_set_header, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr,
          envoy_dynamic_module_type_http_header_type, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_module_buffer)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_send_response,
               envoy_dynamic_module_type_http_filter_envoy_ptr, uint32_t,
               envoy_dynamic_module_type_module_http_header*, size_t,
               envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_send_response_headers,
               envoy_dynamic_module_type_http_filter_envoy_ptr,
               envoy_dynamic_module_type_module_http_header*, size_t, bool)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_send_response_data,
               envoy_dynamic_module_type_http_filter_envoy_ptr,
               envoy_dynamic_module_type_module_buffer, bool)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_send_response_trailers,
               envoy_dynamic_module_type_http_filter_envoy_ptr,
               envoy_dynamic_module_type_module_http_header*, size_t)

WEAK_STUB(size_t, envoy_dynamic_module_callback_http_get_body_size, 0,
          envoy_dynamic_module_type_http_filter_envoy_ptr, envoy_dynamic_module_type_http_body_type)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_get_body_chunks, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr, envoy_dynamic_module_type_http_body_type,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_http_get_body_chunks_size, 0,
          envoy_dynamic_module_type_http_filter_envoy_ptr, envoy_dynamic_module_type_http_body_type)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_append_body, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr, envoy_dynamic_module_type_http_body_type,
          envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_drain_body, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr, envoy_dynamic_module_type_http_body_type,
          size_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_received_buffered_request_body, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_received_buffered_response_body, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_set_dynamic_metadata_number,
               envoy_dynamic_module_type_http_filter_envoy_ptr,
               envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer,
               double)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_get_metadata_number, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr,
          envoy_dynamic_module_type_metadata_source, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_module_buffer, double*)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_set_dynamic_metadata_string,
               envoy_dynamic_module_type_http_filter_envoy_ptr,
               envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer,
               envoy_dynamic_module_type_module_buffer)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_set_dynamic_metadata_string_batch,
               envoy_dynamic_module_type_http_filter_envoy_ptr,
               envoy_dynamic_module_type_module_buffer,
               const envoy_dynamic_module_type_module_key_value_pair*, size_t)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_set_dynamic_metadata_struct,
               envoy_dynamic_module_type_http_filter_envoy_ptr,
               envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_set_dynamic_typed_metadata,
               envoy_dynamic_module_type_http_filter_envoy_ptr,
               envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_get_metadata_string, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr,
          envoy_dynamic_module_type_metadata_source, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_set_dynamic_metadata_bool,
               envoy_dynamic_module_type_http_filter_envoy_ptr,
               envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer,
               bool)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_get_metadata_bool, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr,
          envoy_dynamic_module_type_metadata_source, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_module_buffer, bool*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_http_get_metadata_keys_count, 0,
          envoy_dynamic_module_type_http_filter_envoy_ptr,
          envoy_dynamic_module_type_metadata_source, envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_get_metadata_keys, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr,
          envoy_dynamic_module_type_metadata_source, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(size_t, envoy_dynamic_module_callback_http_get_metadata_namespaces_count, 0,
          envoy_dynamic_module_type_http_filter_envoy_ptr,
          envoy_dynamic_module_type_metadata_source)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_get_metadata_namespaces, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr,
          envoy_dynamic_module_type_metadata_source, envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_set_filter_state_bytes, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_get_filter_state_bytes, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_set_filter_state_typed, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_get_filter_state_typed, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_set_filter_state_object, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_filter_state_object_module_ptr,
          envoy_dynamic_module_type_filter_state_object_destructor,
          envoy_dynamic_module_type_filter_state_life_span)

WEAK_STUB(envoy_dynamic_module_type_filter_state_object_module_ptr,
          envoy_dynamic_module_callback_http_get_filter_state_object, nullptr,
          envoy_dynamic_module_type_http_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_add_custom_flag,
               envoy_dynamic_module_type_http_filter_envoy_ptr,
               envoy_dynamic_module_type_module_buffer)

WEAK_STUB(envoy_dynamic_module_type_http_filter_scheduler_module_ptr,
          envoy_dynamic_module_callback_http_filter_scheduler_new, nullptr,
          envoy_dynamic_module_type_http_filter_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_filter_scheduler_commit,
               envoy_dynamic_module_type_http_filter_scheduler_module_ptr, uint64_t)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_filter_scheduler_delete,
               envoy_dynamic_module_type_http_filter_scheduler_module_ptr)

WEAK_STUB(envoy_dynamic_module_type_http_filter_config_scheduler_module_ptr,
          envoy_dynamic_module_callback_http_filter_config_scheduler_new, nullptr,
          envoy_dynamic_module_type_http_filter_config_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_filter_config_scheduler_delete,
               envoy_dynamic_module_type_http_filter_config_scheduler_module_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_filter_config_scheduler_commit,
               envoy_dynamic_module_type_http_filter_config_scheduler_module_ptr, uint64_t)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_clear_route_cache,
               envoy_dynamic_module_type_http_filter_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_filter_get_attribute_string, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr, envoy_dynamic_module_type_attribute_id,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_filter_get_attribute_int, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr, envoy_dynamic_module_type_attribute_id,
          uint64_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_filter_get_attribute_bool, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr, envoy_dynamic_module_type_attribute_id,
          bool*)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_get_timing_info,
               envoy_dynamic_module_type_http_filter_envoy_ptr,
               envoy_dynamic_module_type_timing_info*)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_filter_get_attribute_string, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr,
          envoy_dynamic_module_type_attribute_id, envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_filter_get_attribute_int, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr,
          envoy_dynamic_module_type_attribute_id, uint64_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_network_filter_get_attribute_bool, false,
          envoy_dynamic_module_type_network_filter_envoy_ptr,
          envoy_dynamic_module_type_attribute_id, bool*)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_get_attribute_string, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_attribute_id, envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_get_attribute_int, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_attribute_id, uint64_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_get_attribute_bool, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_attribute_id, bool*)

WEAK_STUB(envoy_dynamic_module_type_http_callout_init_result,
          envoy_dynamic_module_callback_http_filter_http_callout,
          envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest,
          envoy_dynamic_module_type_http_filter_envoy_ptr, uint64_t*,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_http_header*,
          size_t, envoy_dynamic_module_type_module_buffer, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_http_callout_init_result,
          envoy_dynamic_module_callback_http_filter_start_http_stream,
          envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest,
          envoy_dynamic_module_type_http_filter_envoy_ptr, uint64_t*,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_http_header*,
          size_t, envoy_dynamic_module_type_module_buffer, bool, uint64_t)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_filter_reset_http_stream,
               envoy_dynamic_module_type_http_filter_envoy_ptr, uint64_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_stream_send_data, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr, uint64_t,
          envoy_dynamic_module_type_module_buffer, bool)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_stream_send_trailers, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr, uint64_t,
          envoy_dynamic_module_type_module_http_header*, size_t)

WEAK_STUB(envoy_dynamic_module_type_http_callout_init_result,
          envoy_dynamic_module_callback_http_filter_config_http_callout,
          envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest,
          envoy_dynamic_module_type_http_filter_config_envoy_ptr, uint64_t*,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_http_header*,
          size_t, envoy_dynamic_module_type_module_buffer, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_http_callout_init_result,
          envoy_dynamic_module_callback_http_filter_config_start_http_stream,
          envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest,
          envoy_dynamic_module_type_http_filter_config_envoy_ptr, uint64_t*,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_http_header*,
          size_t, envoy_dynamic_module_type_module_buffer, bool, uint64_t)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_filter_config_reset_http_stream,
               envoy_dynamic_module_type_http_filter_config_envoy_ptr, uint64_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_filter_config_stream_send_data, false,
          envoy_dynamic_module_type_http_filter_config_envoy_ptr, uint64_t,
          envoy_dynamic_module_type_module_buffer, bool)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_filter_config_stream_send_trailers, false,
          envoy_dynamic_module_type_http_filter_config_envoy_ptr, uint64_t,
          envoy_dynamic_module_type_module_http_header*, size_t)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_filter_continue_decoding,
               envoy_dynamic_module_type_http_filter_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_filter_continue_encoding,
               envoy_dynamic_module_type_http_filter_envoy_ptr)

WEAK_STUB(envoy_dynamic_module_type_http_filter_per_route_config_module_ptr,
          envoy_dynamic_module_callback_get_most_specific_route_config, nullptr,
          envoy_dynamic_module_type_http_filter_envoy_ptr)

WEAK_STUB(uint32_t, envoy_dynamic_module_callback_http_filter_get_worker_index, 0,
          envoy_dynamic_module_type_http_filter_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_set_socket_option_int, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr, int64_t, int64_t,
          envoy_dynamic_module_type_socket_option_state, envoy_dynamic_module_type_socket_direction,
          int64_t)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_set_socket_option_bytes, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr, int64_t, int64_t,
          envoy_dynamic_module_type_socket_option_state, envoy_dynamic_module_type_socket_direction,
          envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_get_socket_option_int, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr, int64_t, int64_t,
          envoy_dynamic_module_type_socket_option_state, envoy_dynamic_module_type_socket_direction,
          int64_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_get_socket_option_bytes, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr, int64_t, int64_t,
          envoy_dynamic_module_type_socket_option_state, envoy_dynamic_module_type_socket_direction,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(uint64_t, envoy_dynamic_module_callback_http_get_buffer_limit, 0,
          envoy_dynamic_module_type_http_filter_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_set_buffer_limit,
               envoy_dynamic_module_type_http_filter_envoy_ptr, uint64_t)

WEAK_STUB(envoy_dynamic_module_type_span_envoy_ptr,
          envoy_dynamic_module_callback_http_get_active_span, nullptr,
          envoy_dynamic_module_type_http_filter_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_span_set_tag,
               envoy_dynamic_module_type_span_envoy_ptr, envoy_dynamic_module_type_module_buffer,
               envoy_dynamic_module_type_module_buffer)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_span_set_tag_batch,
               envoy_dynamic_module_type_span_envoy_ptr,
               const envoy_dynamic_module_type_module_key_value_pair*, size_t)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_span_set_operation,
               envoy_dynamic_module_type_span_envoy_ptr, envoy_dynamic_module_type_module_buffer)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_span_log,
               envoy_dynamic_module_type_http_filter_envoy_ptr,
               envoy_dynamic_module_type_span_envoy_ptr, envoy_dynamic_module_type_module_buffer)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_span_set_sampled,
               envoy_dynamic_module_type_span_envoy_ptr, bool)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_span_disable_local_decision,
               envoy_dynamic_module_type_span_envoy_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_span_get_baggage, false,
          envoy_dynamic_module_type_span_envoy_ptr, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_span_set_baggage,
               envoy_dynamic_module_type_span_envoy_ptr, envoy_dynamic_module_type_module_buffer,
               envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_span_get_trace_id, false,
          envoy_dynamic_module_type_span_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_span_get_span_id, false,
          envoy_dynamic_module_type_span_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(envoy_dynamic_module_type_child_span_module_ptr,
          envoy_dynamic_module_callback_http_span_spawn_child, nullptr,
          envoy_dynamic_module_type_http_filter_envoy_ptr, envoy_dynamic_module_type_span_envoy_ptr,
          envoy_dynamic_module_type_module_buffer)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_child_span_finish,
               envoy_dynamic_module_type_child_span_module_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_get_cluster_name, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_get_cluster_host_count, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr, uint32_t, size_t*, size_t*, size_t*)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_set_upstream_override_host, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
          bool)

WEAK_STUB(uint64_t, envoy_dynamic_module_callback_http_get_upstream_connection_id, 0,
          envoy_dynamic_module_type_http_filter_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_filter_reset_stream,
               envoy_dynamic_module_type_http_filter_envoy_ptr,
               envoy_dynamic_module_type_http_filter_stream_reset_reason,
               envoy_dynamic_module_type_module_buffer)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_filter_send_go_away_and_close,
               envoy_dynamic_module_type_http_filter_envoy_ptr, bool)

WEAK_STUB(bool, envoy_dynamic_module_callback_http_filter_recreate_stream, false,
          envoy_dynamic_module_type_http_filter_envoy_ptr,
          envoy_dynamic_module_type_module_http_header*, size_t)

WEAK_STUB_VOID(envoy_dynamic_module_callback_http_clear_route_cluster_cache,
               envoy_dynamic_module_type_http_filter_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_listener_filter_set_detected_transport_protocol,
               envoy_dynamic_module_type_listener_filter_envoy_ptr,
               envoy_dynamic_module_type_module_buffer)

WEAK_STUB_VOID(envoy_dynamic_module_callback_listener_filter_set_requested_server_name,
               envoy_dynamic_module_type_listener_filter_envoy_ptr,
               envoy_dynamic_module_type_module_buffer)

WEAK_STUB_VOID(envoy_dynamic_module_callback_listener_filter_set_requested_application_protocols,
               envoy_dynamic_module_type_listener_filter_envoy_ptr,
               envoy_dynamic_module_type_module_buffer*, size_t)

WEAK_STUB_VOID(envoy_dynamic_module_callback_listener_filter_set_ja3_hash,
               envoy_dynamic_module_type_listener_filter_envoy_ptr,
               envoy_dynamic_module_type_module_buffer)

WEAK_STUB_VOID(envoy_dynamic_module_callback_listener_filter_set_ja4_hash,
               envoy_dynamic_module_type_listener_filter_envoy_ptr,
               envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_set_remote_address, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, uint32_t, bool)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_restore_local_address, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, uint32_t, bool)

WEAK_STUB_VOID(envoy_dynamic_module_callback_listener_filter_continue_filter_chain,
               envoy_dynamic_module_type_listener_filter_envoy_ptr, bool)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_set_filter_state, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_get_filter_state, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_set_filter_state_typed, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer)

WEAK_STUB(bool, envoy_dynamic_module_callback_listener_filter_get_filter_state_typed, false,
          envoy_dynamic_module_type_listener_filter_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_envoy_buffer*)

// ---------------------- Health Checker callbacks ------------------------
// These are weak symbols that provide default stub implementations. The actual implementations
// are provided in the health checker abi_impl.cc when the health checker extension is used.

WEAK_STUB(envoy_dynamic_module_type_health_checker_scheduler_module_ptr,
          envoy_dynamic_module_callback_health_checker_scheduler_new, nullptr,
          envoy_dynamic_module_type_health_checker_session_envoy_ptr)

WEAK_STUB_VOID(envoy_dynamic_module_callback_health_checker_scheduler_report,
               envoy_dynamic_module_type_health_checker_scheduler_module_ptr,
               envoy_dynamic_module_type_host_health)

WEAK_STUB_VOID(envoy_dynamic_module_callback_health_checker_scheduler_delete,
               envoy_dynamic_module_type_health_checker_scheduler_module_ptr)

WEAK_STUB(bool, envoy_dynamic_module_callback_health_checker_get_host_address, false,
          envoy_dynamic_module_type_health_checker_session_envoy_ptr,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_health_checker_get_host_metadata_string, false,
          envoy_dynamic_module_type_health_checker_session_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer,
          envoy_dynamic_module_type_envoy_buffer*)

WEAK_STUB(bool, envoy_dynamic_module_callback_health_checker_get_host_metadata_number, false,
          envoy_dynamic_module_type_health_checker_session_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer, double*)

WEAK_STUB(bool, envoy_dynamic_module_callback_health_checker_get_host_metadata_bool, false,
          envoy_dynamic_module_type_health_checker_session_envoy_ptr,
          envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer, bool*)

WEAK_STUB(envoy_dynamic_module_type_host_health,
          envoy_dynamic_module_callback_health_checker_get_host_health,
          envoy_dynamic_module_type_host_health_Unhealthy,
          envoy_dynamic_module_type_health_checker_session_envoy_ptr)
#undef WEAK_STUB
#undef WEAK_STUB_VOID

} // extern "C"
