// NOLINT(namespace-envoy)

// This file provides host-side implementations for ABI callbacks that are shared across
// all dynamic modules. These are the "Common Callbacks" declared in abi.h and are available
// regardless of which extension point is being used (HTTP/Network/Listener/UDP/Bootstrap/etc).

#include <string>

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

} // namespace

extern "C" {

bool envoy_dynamic_module_callback_log_enabled(envoy_dynamic_module_type_log_level level) {
  return Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules).level() <=
         static_cast<spdlog::level::level_enum>(level);
}

void envoy_dynamic_module_callback_log(envoy_dynamic_module_type_log_level level,
                                       envoy_dynamic_module_type_module_buffer message) {
  absl::string_view message_view(message.ptr, message.length);
  spdlog::logger& logger = Envoy::Logger::Registry::getLog(Envoy::Logger::Id::dynamic_modules);

  switch (level) {
  case envoy_dynamic_module_type_log_level_Trace:
    ENVOY_LOG_TO_LOGGER(logger, trace, "{}", message_view);
    break;
  case envoy_dynamic_module_type_log_level_Debug:
    ENVOY_LOG_TO_LOGGER(logger, debug, "{}", message_view);
    break;
  case envoy_dynamic_module_type_log_level_Info:
    ENVOY_LOG_TO_LOGGER(logger, info, "{}", message_view);
    break;
  case envoy_dynamic_module_type_log_level_Warn:
    ENVOY_LOG_TO_LOGGER(logger, warn, "{}", message_view);
    break;
  case envoy_dynamic_module_type_log_level_Error:
    ENVOY_LOG_TO_LOGGER(logger, error, "{}", message_view);
    break;
  case envoy_dynamic_module_type_log_level_Critical:
    ENVOY_LOG_TO_LOGGER(logger, critical, "{}", message_view);
    break;
  default:
    break;
  }
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

// ---------------------- Function registry callbacks --------------------------------

bool envoy_dynamic_module_callback_register_function(envoy_dynamic_module_type_module_buffer key,
                                                     void* function_ptr) {
  if (function_ptr == nullptr) {
    return false;
  }
  std::string key_str(key.ptr, key.length);
  absl::WriterMutexLock lock(function_registry_mutex);
  auto [it, inserted] = function_registry.try_emplace(key_str, function_ptr);
  return inserted;
}

bool envoy_dynamic_module_callback_get_function(envoy_dynamic_module_type_module_buffer key,
                                                void** function_ptr_out) {
  std::string key_str(key.ptr, key.length);
  absl::ReaderMutexLock lock(function_registry_mutex);
  auto it = function_registry.find(key_str);
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
  std::string key_str(key.ptr, key.length);
  absl::WriterMutexLock lock(shared_data_registry_mutex);
  shared_data_registry[key_str] = data_ptr;
  return true;
}

bool envoy_dynamic_module_callback_get_shared_data(envoy_dynamic_module_type_module_buffer key,
                                                   void** data_ptr_out) {
  std::string key_str(key.ptr, key.length);
  absl::ReaderMutexLock lock(shared_data_registry_mutex);
  auto it = shared_data_registry.find(key_str);
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

__attribute__((weak)) envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr
envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_new: "
               "not implemented in this context");
  return nullptr;
}

__attribute__((weak)) void
envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete(
    envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_delete: "
               "not implemented in this context");
}

__attribute__((weak)) void
envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_commit(
    envoy_dynamic_module_type_bootstrap_extension_config_scheduler_module_ptr, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_config_scheduler_commit: "
               "not implemented in this context");
}

__attribute__((weak)) void
envoy_dynamic_module_callback_bootstrap_extension_config_signal_init_complete(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_config_signal_init_complete: "
               "not implemented in this context");
}

__attribute__((weak)) envoy_dynamic_module_type_http_callout_init_result
envoy_dynamic_module_callback_bootstrap_extension_http_callout(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr, uint64_t* /* callout_id_out */,
    envoy_dynamic_module_type_module_buffer /* cluster_name */,
    envoy_dynamic_module_type_module_http_header* /* headers */, size_t /* headers_size */,
    envoy_dynamic_module_type_module_buffer /* body */, uint64_t /* timeout_milliseconds */) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_http_callout: "
               "not implemented in this context");
  return envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest;
}

// ---------------------- Bootstrap extension stats access callbacks ------------------------
// These are weak symbols that provide default stub implementations. The actual implementations
// are provided in the bootstrap extension abi_impl.cc when the bootstrap extension is used.

__attribute__((weak)) bool envoy_dynamic_module_callback_bootstrap_extension_get_counter_value(
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
    envoy_dynamic_module_type_module_buffer, uint64_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_get_counter_value: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_bootstrap_extension_get_gauge_value(
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
    envoy_dynamic_module_type_module_buffer, uint64_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_get_gauge_value: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_bootstrap_extension_get_histogram_summary(
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
    envoy_dynamic_module_type_module_buffer, uint64_t*, double*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_get_histogram_summary: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) void envoy_dynamic_module_callback_bootstrap_extension_iterate_counters(
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
    envoy_dynamic_module_type_counter_iterator_fn, void*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_iterate_counters: "
               "not implemented in this context");
}

__attribute__((weak)) void envoy_dynamic_module_callback_bootstrap_extension_iterate_gauges(
    envoy_dynamic_module_type_bootstrap_extension_envoy_ptr,
    envoy_dynamic_module_type_gauge_iterator_fn, void*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_iterate_gauges: "
               "not implemented in this context");
}

// ---------------------- Bootstrap extension stats definition and update callbacks
// --------------------- These are weak symbols that provide default stub implementations. The
// actual implementations are provided in the bootstrap extension abi_impl.cc when the bootstrap
// extension is used.

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_define_counter(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer*, size_t,
    size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_config_define_counter: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_increment_counter(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr, size_t,
    envoy_dynamic_module_type_module_buffer*, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_config_increment_counter: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_define_gauge(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer*, size_t,
    size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_config_define_gauge: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_set_gauge(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr, size_t,
    envoy_dynamic_module_type_module_buffer*, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_config_set_gauge: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_increment_gauge(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr, size_t,
    envoy_dynamic_module_type_module_buffer*, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_config_increment_gauge: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_decrement_gauge(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr, size_t,
    envoy_dynamic_module_type_module_buffer*, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_config_decrement_gauge: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_define_histogram(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer*, size_t,
    size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_config_define_histogram: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_bootstrap_extension_config_record_histogram_value(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr, size_t,
    envoy_dynamic_module_type_module_buffer*, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_config_record_histogram_value: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

// ---------------------- Cert Validator callbacks ------------------------
// These are weak symbols that provide default stub implementations. The actual implementation
// is provided in the cert validator config.cc when the cert validator extension is used.

__attribute__((weak)) void envoy_dynamic_module_callback_cert_validator_set_error_details(
    envoy_dynamic_module_type_cert_validator_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cert_validator_set_error_details: "
               "not implemented in this context");
}

__attribute__((weak)) bool envoy_dynamic_module_callback_cert_validator_set_filter_state(
    envoy_dynamic_module_type_cert_validator_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cert_validator_set_filter_state: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_cert_validator_get_filter_state(
    envoy_dynamic_module_type_cert_validator_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cert_validator_get_filter_state: "
               "not implemented in this context");
  return false;
}

// ---------------------- Bootstrap extension admin handler callbacks ------------------------
// These are weak symbols that provide default stub implementations. The actual implementations
// are provided in the bootstrap extension abi_impl.cc when the bootstrap extension is used.

__attribute__((weak)) void envoy_dynamic_module_callback_bootstrap_extension_admin_set_response(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_admin_set_response: "
               "not implemented in this context");
}

__attribute__((weak)) bool envoy_dynamic_module_callback_bootstrap_extension_register_admin_handler(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer, bool, bool) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_register_admin_handler: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_bootstrap_extension_remove_admin_handler(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_remove_admin_handler: "
               "not implemented in this context");
  return false;
}

// ---------------------- Bootstrap extension timer callbacks ------------------------
// These are weak symbols that provide default stub implementations. The actual implementations
// are provided in the bootstrap extension abi_impl.cc when the bootstrap extension is used.

__attribute__((weak)) envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr
envoy_dynamic_module_callback_bootstrap_extension_timer_new(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_timer_new: "
               "not implemented in this context");
  return nullptr;
}

__attribute__((weak)) void envoy_dynamic_module_callback_bootstrap_extension_timer_enable(
    envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_timer_enable: "
               "not implemented in this context");
}

__attribute__((weak)) void envoy_dynamic_module_callback_bootstrap_extension_timer_disable(
    envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_timer_disable: "
               "not implemented in this context");
}

__attribute__((weak)) bool envoy_dynamic_module_callback_bootstrap_extension_timer_enabled(
    envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_timer_enabled: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) void envoy_dynamic_module_callback_bootstrap_extension_timer_delete(
    envoy_dynamic_module_type_bootstrap_extension_timer_module_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_timer_delete: "
               "not implemented in this context");
}

__attribute__((weak)) bool envoy_dynamic_module_callback_bootstrap_extension_file_watcher_add_watch(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer, uint32_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_file_watcher_add_watch: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool
envoy_dynamic_module_callback_bootstrap_extension_enable_cluster_lifecycle(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_enable_cluster_lifecycle: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool
envoy_dynamic_module_callback_bootstrap_extension_enable_listener_lifecycle(
    envoy_dynamic_module_type_bootstrap_extension_config_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_bootstrap_extension_enable_listener_lifecycle: "
               "not implemented in this context");
  return false;
}

// ---------------------- Cluster extension callbacks ------------------------
// These are weak symbols that provide default stub implementations. The actual implementations
// are provided in the cluster extension abi_impl.cc when the cluster extension is used.

__attribute__((weak)) bool envoy_dynamic_module_callback_cluster_add_hosts(
    envoy_dynamic_module_type_cluster_envoy_ptr, uint32_t,
    const envoy_dynamic_module_type_module_buffer*, const uint32_t*,
    const envoy_dynamic_module_type_module_buffer*, const envoy_dynamic_module_type_module_buffer*,
    const envoy_dynamic_module_type_module_buffer*, const envoy_dynamic_module_type_module_buffer*,
    size_t, size_t, envoy_dynamic_module_type_cluster_host_envoy_ptr*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_add_hosts: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) size_t envoy_dynamic_module_callback_cluster_remove_hosts(
    envoy_dynamic_module_type_cluster_envoy_ptr,
    const envoy_dynamic_module_type_cluster_host_envoy_ptr*, size_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_remove_hosts: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_cluster_update_host_health(
    envoy_dynamic_module_type_cluster_envoy_ptr, envoy_dynamic_module_type_cluster_host_envoy_ptr,
    envoy_dynamic_module_type_host_health) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_update_host_health: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) envoy_dynamic_module_type_cluster_host_envoy_ptr
envoy_dynamic_module_callback_cluster_find_host_by_address(
    envoy_dynamic_module_type_cluster_envoy_ptr, envoy_dynamic_module_type_module_buffer) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_find_host_by_address: "
               "not implemented in this context");
  return nullptr;
}

__attribute__((weak)) envoy_dynamic_module_type_cluster_host_envoy_ptr
envoy_dynamic_module_callback_cluster_lb_find_host_by_address(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr, envoy_dynamic_module_type_module_buffer) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_find_host_by_address: "
               "not implemented in this context");
  return nullptr;
}

__attribute__((weak)) envoy_dynamic_module_type_cluster_host_envoy_ptr
envoy_dynamic_module_callback_cluster_lb_get_host(envoy_dynamic_module_type_cluster_lb_envoy_ptr,
                                                  uint32_t, size_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_get_host: "
               "not implemented in this context");
  return nullptr;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_cluster_lb_get_member_update_host_address(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr, size_t, bool,
    envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_get_member_update_host_address: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) void envoy_dynamic_module_callback_cluster_pre_init_complete(
    envoy_dynamic_module_type_cluster_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_pre_init_complete: "
               "not implemented in this context");
}

__attribute__((weak)) size_t envoy_dynamic_module_callback_cluster_lb_get_healthy_host_count(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_get_healthy_host_count: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) envoy_dynamic_module_type_cluster_host_envoy_ptr
envoy_dynamic_module_callback_cluster_lb_get_healthy_host(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_get_healthy_host: "
               "not implemented in this context");
  return nullptr;
}

__attribute__((weak)) void envoy_dynamic_module_callback_cluster_lb_get_cluster_name(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_get_cluster_name: "
               "not implemented in this context");
}

__attribute__((weak)) size_t envoy_dynamic_module_callback_cluster_lb_get_hosts_count(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_get_hosts_count: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) size_t envoy_dynamic_module_callback_cluster_lb_get_degraded_hosts_count(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_get_degraded_hosts_count: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) size_t envoy_dynamic_module_callback_cluster_lb_get_priority_set_size(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_get_priority_set_size: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_cluster_lb_get_healthy_host_address(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t,
    envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_get_healthy_host_address: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) uint32_t envoy_dynamic_module_callback_cluster_lb_get_healthy_host_weight(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_get_healthy_host_weight: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) envoy_dynamic_module_type_host_health
envoy_dynamic_module_callback_cluster_lb_get_host_health(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_get_host_health: "
               "not implemented in this context");
  return envoy_dynamic_module_type_host_health_Unhealthy;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_cluster_lb_get_host_health_by_address(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_host_health*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_get_host_health_by_address: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_cluster_lb_get_host_address(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t,
    envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_get_host_address: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) uint32_t envoy_dynamic_module_callback_cluster_lb_get_host_weight(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_get_host_weight: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) uint64_t envoy_dynamic_module_callback_cluster_lb_get_host_stat(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t,
    envoy_dynamic_module_type_host_stat) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_get_host_stat: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_cluster_lb_get_host_locality(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t,
    envoy_dynamic_module_type_envoy_buffer*, envoy_dynamic_module_type_envoy_buffer*,
    envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_get_host_locality: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_cluster_lb_set_host_data(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t, uintptr_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_set_host_data: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_cluster_lb_get_host_data(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t, uintptr_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_get_host_data: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_cluster_lb_get_host_metadata_string(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_get_host_metadata_string: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_cluster_lb_get_host_metadata_number(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer, double*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_get_host_metadata_number: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_cluster_lb_get_host_metadata_bool(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer, bool*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_get_host_metadata_bool: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) size_t envoy_dynamic_module_callback_cluster_lb_get_locality_count(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_get_locality_count: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) size_t envoy_dynamic_module_callback_cluster_lb_get_locality_host_count(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_get_locality_host_count: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_cluster_lb_get_locality_host_address(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t, size_t,
    envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_get_locality_host_address: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) uint32_t envoy_dynamic_module_callback_cluster_lb_get_locality_weight(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr, uint32_t, size_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_get_locality_weight: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_cluster_lb_context_compute_hash_key(
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr, uint64_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_context_compute_hash_key: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) size_t
envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers_size(
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers_size: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers(
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
    envoy_dynamic_module_type_envoy_http_header*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_context_get_downstream_headers: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_cluster_lb_context_get_downstream_header(
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_envoy_buffer*, size_t, size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_context_get_downstream_header: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) uint32_t
envoy_dynamic_module_callback_cluster_lb_context_get_host_selection_retry_count(
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_context_get_host_selection_retry_count: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) bool
envoy_dynamic_module_callback_cluster_lb_context_should_select_another_host(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr,
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr, uint32_t, size_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_context_should_select_another_host: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_cluster_lb_context_get_override_host(
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*,
    bool*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_context_get_override_host: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool
envoy_dynamic_module_callback_cluster_lb_context_get_downstream_connection_sni(
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_context_get_downstream_connection_sni: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) envoy_dynamic_module_type_cluster_scheduler_module_ptr
envoy_dynamic_module_callback_cluster_scheduler_new(envoy_dynamic_module_type_cluster_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_scheduler_new: "
               "not implemented in this context");
  return nullptr;
}

__attribute__((weak)) void envoy_dynamic_module_callback_cluster_scheduler_delete(
    envoy_dynamic_module_type_cluster_scheduler_module_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_scheduler_delete: "
               "not implemented in this context");
}

__attribute__((weak)) void envoy_dynamic_module_callback_cluster_scheduler_commit(
    envoy_dynamic_module_type_cluster_scheduler_module_ptr, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_scheduler_commit: "
               "not implemented in this context");
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_config_define_counter(
    envoy_dynamic_module_type_cluster_config_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_module_buffer*, size_t, size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_config_define_counter: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_config_increment_counter(
    envoy_dynamic_module_type_cluster_config_envoy_ptr, size_t,
    envoy_dynamic_module_type_module_buffer*, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_config_increment_counter: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_config_define_gauge(
    envoy_dynamic_module_type_cluster_config_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_module_buffer*, size_t, size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_config_define_gauge: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_config_set_gauge(
    envoy_dynamic_module_type_cluster_config_envoy_ptr, size_t,
    envoy_dynamic_module_type_module_buffer*, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_config_set_gauge: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_config_increment_gauge(
    envoy_dynamic_module_type_cluster_config_envoy_ptr, size_t,
    envoy_dynamic_module_type_module_buffer*, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_config_increment_gauge: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_config_decrement_gauge(
    envoy_dynamic_module_type_cluster_config_envoy_ptr, size_t,
    envoy_dynamic_module_type_module_buffer*, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_config_decrement_gauge: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_config_define_histogram(
    envoy_dynamic_module_type_cluster_config_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_module_buffer*, size_t, size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_config_define_histogram: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_cluster_config_record_histogram_value(
    envoy_dynamic_module_type_cluster_config_envoy_ptr, size_t,
    envoy_dynamic_module_type_module_buffer*, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_config_record_histogram_value: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) void envoy_dynamic_module_callback_cluster_lb_async_host_selection_complete(
    envoy_dynamic_module_type_cluster_lb_envoy_ptr,
    envoy_dynamic_module_type_cluster_lb_context_envoy_ptr,
    envoy_dynamic_module_type_cluster_host_envoy_ptr, envoy_dynamic_module_type_module_buffer) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_lb_async_host_selection_complete: "
               "not implemented in this context");
}

__attribute__((weak)) envoy_dynamic_module_type_http_callout_init_result
envoy_dynamic_module_callback_cluster_http_callout(
    envoy_dynamic_module_type_cluster_envoy_ptr, uint64_t* /* callout_id_out */,
    envoy_dynamic_module_type_module_buffer /* cluster_name */,
    envoy_dynamic_module_type_module_http_header* /* headers */, size_t /* headers_size */,
    envoy_dynamic_module_type_module_buffer /* body */, uint64_t /* timeout_milliseconds */) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_cluster_http_callout: "
               "not implemented in this context");
  return envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest;
}

// ---------------------- Load Balancer callbacks ------------------------
// These are weak symbols that provide default stub implementations. The actual implementations
// are provided in the load balancing policy extension abi_impl.cc when the extension is used.

__attribute__((weak)) void
envoy_dynamic_module_callback_lb_get_cluster_name(envoy_dynamic_module_type_lb_envoy_ptr,
                                                  envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_get_cluster_name: "
               "not implemented in this context");
}

__attribute__((weak)) size_t
envoy_dynamic_module_callback_lb_get_hosts_count(envoy_dynamic_module_type_lb_envoy_ptr, uint32_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_get_hosts_count: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) size_t envoy_dynamic_module_callback_lb_get_healthy_hosts_count(
    envoy_dynamic_module_type_lb_envoy_ptr, uint32_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_get_healthy_hosts_count: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) size_t envoy_dynamic_module_callback_lb_get_degraded_hosts_count(
    envoy_dynamic_module_type_lb_envoy_ptr, uint32_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_get_degraded_hosts_count: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) size_t
envoy_dynamic_module_callback_lb_get_priority_set_size(envoy_dynamic_module_type_lb_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_get_priority_set_size: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) bool
envoy_dynamic_module_callback_lb_get_healthy_host_address(envoy_dynamic_module_type_lb_envoy_ptr,
                                                          uint32_t, size_t,
                                                          envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_get_healthy_host_address: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) uint32_t envoy_dynamic_module_callback_lb_get_healthy_host_weight(
    envoy_dynamic_module_type_lb_envoy_ptr, uint32_t, size_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_get_healthy_host_weight: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) envoy_dynamic_module_type_host_health
envoy_dynamic_module_callback_lb_get_host_health(envoy_dynamic_module_type_lb_envoy_ptr, uint32_t,
                                                 size_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_get_host_health: "
               "not implemented in this context");
  return envoy_dynamic_module_type_host_health_Unhealthy;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_lb_get_host_health_by_address(
    envoy_dynamic_module_type_lb_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_host_health*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_get_host_health_by_address: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool
envoy_dynamic_module_callback_lb_get_host_address(envoy_dynamic_module_type_lb_envoy_ptr, uint32_t,
                                                  size_t, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_get_host_address: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) uint32_t envoy_dynamic_module_callback_lb_get_host_weight(
    envoy_dynamic_module_type_lb_envoy_ptr, uint32_t, size_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_get_host_weight: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) bool
envoy_dynamic_module_callback_lb_get_host_locality(envoy_dynamic_module_type_lb_envoy_ptr, uint32_t,
                                                   size_t, envoy_dynamic_module_type_envoy_buffer*,
                                                   envoy_dynamic_module_type_envoy_buffer*,
                                                   envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_get_host_locality: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_lb_context_compute_hash_key(
    envoy_dynamic_module_type_lb_context_envoy_ptr, uint64_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_context_compute_hash_key: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) size_t envoy_dynamic_module_callback_lb_context_get_downstream_headers_size(
    envoy_dynamic_module_type_lb_context_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_context_get_downstream_headers_size: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_lb_context_get_downstream_headers(
    envoy_dynamic_module_type_lb_context_envoy_ptr, envoy_dynamic_module_type_envoy_http_header*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_context_get_downstream_headers: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_lb_context_get_downstream_header(
    envoy_dynamic_module_type_lb_context_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_envoy_buffer*, size_t, size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_context_get_downstream_header: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) uint32_t
envoy_dynamic_module_callback_lb_context_get_host_selection_retry_count(
    envoy_dynamic_module_type_lb_context_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_context_get_host_selection_retry_count: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_lb_context_should_select_another_host(
    envoy_dynamic_module_type_lb_envoy_ptr, envoy_dynamic_module_type_lb_context_envoy_ptr,
    uint32_t, size_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_context_should_select_another_host: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_lb_context_get_override_host(
    envoy_dynamic_module_type_lb_context_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*,
    bool*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_context_get_override_host: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool
envoy_dynamic_module_callback_lb_set_host_data(envoy_dynamic_module_type_lb_envoy_ptr, uint32_t,
                                               size_t, uintptr_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_set_host_data: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool
envoy_dynamic_module_callback_lb_get_host_data(envoy_dynamic_module_type_lb_envoy_ptr, uint32_t,
                                               size_t, uintptr_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_get_host_data: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_lb_get_host_metadata_string(
    envoy_dynamic_module_type_lb_envoy_ptr, uint32_t, size_t,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_get_host_metadata_string: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_lb_get_host_metadata_number(
    envoy_dynamic_module_type_lb_envoy_ptr, uint32_t, size_t,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer, double*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_get_host_metadata_number: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_lb_get_host_metadata_bool(
    envoy_dynamic_module_type_lb_envoy_ptr, uint32_t, size_t,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer, bool*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_get_host_metadata_bool: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) size_t envoy_dynamic_module_callback_lb_get_locality_count(
    envoy_dynamic_module_type_lb_envoy_ptr, uint32_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_get_locality_count: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) size_t envoy_dynamic_module_callback_lb_get_locality_host_count(
    envoy_dynamic_module_type_lb_envoy_ptr, uint32_t, size_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_get_locality_host_count: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_lb_get_locality_host_address(
    envoy_dynamic_module_type_lb_envoy_ptr, uint32_t, size_t, size_t,
    envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_get_locality_host_address: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) uint32_t envoy_dynamic_module_callback_lb_get_locality_weight(
    envoy_dynamic_module_type_lb_envoy_ptr, uint32_t, size_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_get_locality_weight: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_lb_get_member_update_host_address(
    envoy_dynamic_module_type_lb_envoy_ptr, size_t, bool, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_get_member_update_host_address: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) uint64_t envoy_dynamic_module_callback_lb_get_host_stat(
    envoy_dynamic_module_type_lb_envoy_ptr, uint32_t, size_t, envoy_dynamic_module_type_host_stat) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_get_host_stat: not implemented in this context");
  return 0;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_lb_config_define_counter(
    envoy_dynamic_module_type_lb_config_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_module_buffer*, size_t, size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_config_define_counter: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_lb_config_increment_counter(
    envoy_dynamic_module_type_lb_config_envoy_ptr, size_t, envoy_dynamic_module_type_module_buffer*,
    size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_config_increment_counter: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_lb_config_define_gauge(envoy_dynamic_module_type_lb_config_envoy_ptr,
                                                     envoy_dynamic_module_type_module_buffer,
                                                     envoy_dynamic_module_type_module_buffer*,
                                                     size_t, size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_config_define_gauge: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_lb_config_set_gauge(envoy_dynamic_module_type_lb_config_envoy_ptr,
                                                  size_t, envoy_dynamic_module_type_module_buffer*,
                                                  size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_config_set_gauge: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_lb_config_increment_gauge(
    envoy_dynamic_module_type_lb_config_envoy_ptr, size_t, envoy_dynamic_module_type_module_buffer*,
    size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_config_increment_gauge: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_lb_config_decrement_gauge(
    envoy_dynamic_module_type_lb_config_envoy_ptr, size_t, envoy_dynamic_module_type_module_buffer*,
    size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_config_decrement_gauge: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_lb_config_define_histogram(
    envoy_dynamic_module_type_lb_config_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_module_buffer*, size_t, size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_config_define_histogram: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_lb_config_record_histogram_value(
    envoy_dynamic_module_type_lb_config_envoy_ptr, size_t, envoy_dynamic_module_type_module_buffer*,
    size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_lb_config_record_histogram_value: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

// ---------------------- Matcher callbacks ------------------------
// These are weak symbols that provide default stub implementations. The actual implementations
// are provided in the matcher extension abi_impl.cc when the matcher extension is used.

__attribute__((weak)) size_t envoy_dynamic_module_callback_matcher_get_headers_size(
    envoy_dynamic_module_type_matcher_input_envoy_ptr, envoy_dynamic_module_type_http_header_type) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_matcher_get_headers_size: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) bool
envoy_dynamic_module_callback_matcher_get_headers(envoy_dynamic_module_type_matcher_input_envoy_ptr,
                                                  envoy_dynamic_module_type_http_header_type,
                                                  envoy_dynamic_module_type_envoy_http_header*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_matcher_get_headers: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_matcher_get_header_value(
    envoy_dynamic_module_type_matcher_input_envoy_ptr, envoy_dynamic_module_type_http_header_type,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_envoy_buffer*, size_t,
    size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_matcher_get_header_value: "
               "not implemented in this context");
  return false;
}

// ---------------------- Network filter callbacks ------------------------
// These are weak symbols that provide default stub implementations. The actual implementations
// are provided in the network filter abi_impl.cc when the network filter extension is used.

__attribute__((weak)) size_t
envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks_size(
    envoy_dynamic_module_type_network_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks_size: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) size_t envoy_dynamic_module_callback_network_filter_get_read_buffer_size(
    envoy_dynamic_module_type_network_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_get_read_buffer_size: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_get_read_buffer_chunks: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) size_t
envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks_size(
    envoy_dynamic_module_type_network_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks_size: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) size_t envoy_dynamic_module_callback_network_filter_get_write_buffer_size(
    envoy_dynamic_module_type_network_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_get_write_buffer_size: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_get_write_buffer_chunks: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_filter_drain_read_buffer(
    envoy_dynamic_module_type_network_filter_envoy_ptr, size_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_drain_read_buffer: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_filter_drain_write_buffer(
    envoy_dynamic_module_type_network_filter_envoy_ptr, size_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_drain_write_buffer: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_filter_prepend_read_buffer(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_prepend_read_buffer: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_filter_append_read_buffer(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_append_read_buffer: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_filter_prepend_write_buffer(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_prepend_write_buffer: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_filter_append_write_buffer(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_append_write_buffer: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) void envoy_dynamic_module_callback_network_filter_write(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    bool) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_write: "
               "not implemented in this context");
}

__attribute__((weak)) void envoy_dynamic_module_callback_network_filter_inject_read_data(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    bool) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_inject_read_data: "
               "not implemented in this context");
}

__attribute__((weak)) void envoy_dynamic_module_callback_network_filter_inject_write_data(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    bool) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_inject_write_data: "
               "not implemented in this context");
}

__attribute__((weak)) void envoy_dynamic_module_callback_network_filter_continue_reading(
    envoy_dynamic_module_type_network_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_continue_reading: "
               "not implemented in this context");
}

__attribute__((weak)) void envoy_dynamic_module_callback_network_filter_close(
    envoy_dynamic_module_type_network_filter_envoy_ptr,
    envoy_dynamic_module_type_network_connection_close_type) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_close: "
               "not implemented in this context");
}

__attribute__((weak)) uint64_t envoy_dynamic_module_callback_network_filter_get_connection_id(
    envoy_dynamic_module_type_network_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_get_connection_id: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_filter_get_remote_address(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*,
    uint32_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_get_remote_address: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_filter_get_local_address(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*,
    uint32_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_get_local_address: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_filter_is_ssl(
    envoy_dynamic_module_type_network_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_is_ssl: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) void envoy_dynamic_module_callback_network_filter_disable_close(
    envoy_dynamic_module_type_network_filter_envoy_ptr, bool) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_disable_close: "
               "not implemented in this context");
}

__attribute__((weak)) void envoy_dynamic_module_callback_network_filter_close_with_details(
    envoy_dynamic_module_type_network_filter_envoy_ptr,
    envoy_dynamic_module_type_network_connection_close_type,
    envoy_dynamic_module_type_module_buffer) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_close_with_details: "
               "not implemented in this context");
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_filter_get_requested_server_name(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_get_requested_server_name: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_filter_get_direct_remote_address(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*,
    uint32_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_get_direct_remote_address: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) size_t envoy_dynamic_module_callback_network_filter_get_ssl_uri_sans_size(
    envoy_dynamic_module_type_network_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_get_ssl_uri_sans_size: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_filter_get_ssl_uri_sans(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_get_ssl_uri_sans: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) size_t envoy_dynamic_module_callback_network_filter_get_ssl_dns_sans_size(
    envoy_dynamic_module_type_network_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_get_ssl_dns_sans_size: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_filter_get_ssl_dns_sans(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_get_ssl_dns_sans: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_filter_get_ssl_subject(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_get_ssl_subject: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_set_filter_state_bytes(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_module_buffer) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_set_filter_state_bytes: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_get_filter_state_bytes(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_get_filter_state_bytes: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_set_filter_state_typed(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_module_buffer) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_set_filter_state_typed: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_get_filter_state_typed(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_get_filter_state_typed: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) void envoy_dynamic_module_callback_network_set_dynamic_metadata_string(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_set_dynamic_metadata_string: "
               "not implemented in this context");
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_get_dynamic_metadata_string(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_get_dynamic_metadata_string: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) void envoy_dynamic_module_callback_network_set_dynamic_metadata_number(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_module_buffer, double) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_set_dynamic_metadata_number: "
               "not implemented in this context");
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_get_dynamic_metadata_number(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_module_buffer, double*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_get_dynamic_metadata_number: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) void envoy_dynamic_module_callback_network_set_dynamic_metadata_bool(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_module_buffer, bool) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_set_dynamic_metadata_bool: "
               "not implemented in this context");
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_get_dynamic_metadata_bool(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_module_buffer, bool*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_get_dynamic_metadata_bool: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) envoy_dynamic_module_type_http_callout_init_result
envoy_dynamic_module_callback_network_filter_http_callout(
    envoy_dynamic_module_type_network_filter_envoy_ptr, uint64_t*,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_http_header*, size_t,
    envoy_dynamic_module_type_module_buffer, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_http_callout: "
               "not implemented in this context");
  return envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_network_filter_config_define_counter(
    envoy_dynamic_module_type_network_filter_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer, size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_config_define_counter: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_network_filter_increment_counter(
    envoy_dynamic_module_type_network_filter_envoy_ptr, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_increment_counter: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_network_filter_config_define_gauge(
    envoy_dynamic_module_type_network_filter_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer, size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_config_define_gauge: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_network_filter_set_gauge(
    envoy_dynamic_module_type_network_filter_envoy_ptr, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_set_gauge: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_network_filter_increment_gauge(
    envoy_dynamic_module_type_network_filter_envoy_ptr, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_increment_gauge: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_network_filter_decrement_gauge(
    envoy_dynamic_module_type_network_filter_envoy_ptr, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_decrement_gauge: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_network_filter_config_define_histogram(
    envoy_dynamic_module_type_network_filter_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer, size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_config_define_histogram: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_network_filter_record_histogram_value(
    envoy_dynamic_module_type_network_filter_envoy_ptr, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_record_histogram_value: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_filter_get_cluster_host_count(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    uint32_t, size_t*, size_t*, size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_get_cluster_host_count: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_filter_get_upstream_host_address(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*,
    uint32_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_get_upstream_host_address: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_filter_get_upstream_host_hostname(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_get_upstream_host_hostname: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_filter_get_upstream_host_cluster(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_get_upstream_host_cluster: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_filter_has_upstream_host(
    envoy_dynamic_module_type_network_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_has_upstream_host: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool
envoy_dynamic_module_callback_network_filter_start_upstream_secure_transport(
    envoy_dynamic_module_type_network_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_start_upstream_secure_transport: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) envoy_dynamic_module_type_network_connection_state
envoy_dynamic_module_callback_network_filter_get_connection_state(
    envoy_dynamic_module_type_network_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_get_connection_state: "
               "not implemented in this context");
  return envoy_dynamic_module_type_network_connection_state_Closed;
}

__attribute__((weak)) envoy_dynamic_module_type_network_read_disable_status
envoy_dynamic_module_callback_network_filter_read_disable(
    envoy_dynamic_module_type_network_filter_envoy_ptr, bool) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_read_disable: "
               "not implemented in this context");
  return envoy_dynamic_module_type_network_read_disable_status_NoTransition;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_filter_read_enabled(
    envoy_dynamic_module_type_network_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_read_enabled: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_filter_is_half_close_enabled(
    envoy_dynamic_module_type_network_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_is_half_close_enabled: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) void envoy_dynamic_module_callback_network_filter_enable_half_close(
    envoy_dynamic_module_type_network_filter_envoy_ptr, bool) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_enable_half_close: "
               "not implemented in this context");
}

__attribute__((weak)) uint32_t envoy_dynamic_module_callback_network_filter_get_buffer_limit(
    envoy_dynamic_module_type_network_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_get_buffer_limit: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) void envoy_dynamic_module_callback_network_filter_set_buffer_limits(
    envoy_dynamic_module_type_network_filter_envoy_ptr, uint32_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_set_buffer_limits: "
               "not implemented in this context");
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_filter_above_high_watermark(
    envoy_dynamic_module_type_network_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_above_high_watermark: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) envoy_dynamic_module_type_network_filter_scheduler_module_ptr
envoy_dynamic_module_callback_network_filter_scheduler_new(
    envoy_dynamic_module_type_network_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_scheduler_new: "
               "not implemented in this context");
  return nullptr;
}

__attribute__((weak)) void envoy_dynamic_module_callback_network_filter_scheduler_commit(
    envoy_dynamic_module_type_network_filter_scheduler_module_ptr, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_scheduler_commit: "
               "not implemented in this context");
}

__attribute__((weak)) void envoy_dynamic_module_callback_network_filter_scheduler_delete(
    envoy_dynamic_module_type_network_filter_scheduler_module_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_scheduler_delete: "
               "not implemented in this context");
}

__attribute__((weak)) envoy_dynamic_module_type_network_filter_config_scheduler_module_ptr
envoy_dynamic_module_callback_network_filter_config_scheduler_new(
    envoy_dynamic_module_type_network_filter_config_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_config_scheduler_new: "
               "not implemented in this context");
  return nullptr;
}

__attribute__((weak)) void envoy_dynamic_module_callback_network_filter_config_scheduler_delete(
    envoy_dynamic_module_type_network_filter_config_scheduler_module_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_config_scheduler_delete: "
               "not implemented in this context");
}

__attribute__((weak)) void envoy_dynamic_module_callback_network_filter_config_scheduler_commit(
    envoy_dynamic_module_type_network_filter_config_scheduler_module_ptr, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_config_scheduler_commit: "
               "not implemented in this context");
}

__attribute__((weak)) uint32_t envoy_dynamic_module_callback_network_filter_get_worker_index(
    envoy_dynamic_module_type_network_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_filter_get_worker_index: "
               "not implemented in this context");
  return 0;
}

// ---------------------- Socket Option Callbacks (Network) --------------------

__attribute__((weak)) void envoy_dynamic_module_callback_network_set_socket_option_int(
    envoy_dynamic_module_type_network_filter_envoy_ptr, int64_t, int64_t,
    envoy_dynamic_module_type_socket_option_state, int64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_set_socket_option_int: "
               "not implemented in this context");
}

__attribute__((weak)) void envoy_dynamic_module_callback_network_set_socket_option_bytes(
    envoy_dynamic_module_type_network_filter_envoy_ptr, int64_t, int64_t,
    envoy_dynamic_module_type_socket_option_state, envoy_dynamic_module_type_module_buffer) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_set_socket_option_bytes: "
               "not implemented in this context");
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_get_socket_option_int(
    envoy_dynamic_module_type_network_filter_envoy_ptr, int64_t, int64_t,
    envoy_dynamic_module_type_socket_option_state, int64_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_get_socket_option_int: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_network_get_socket_option_bytes(
    envoy_dynamic_module_type_network_filter_envoy_ptr, int64_t, int64_t,
    envoy_dynamic_module_type_socket_option_state, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_get_socket_option_bytes: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) size_t envoy_dynamic_module_callback_network_get_socket_options_size(
    envoy_dynamic_module_type_network_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_get_socket_options_size: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) void envoy_dynamic_module_callback_network_get_socket_options(
    envoy_dynamic_module_type_network_filter_envoy_ptr, envoy_dynamic_module_type_socket_option*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_network_get_socket_options: "
               "not implemented in this context");
}

// ---------------------- Listener Filter Callbacks ----------------------------

__attribute__((weak)) bool envoy_dynamic_module_callback_listener_filter_get_buffer_chunk(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_get_buffer_chunk: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_listener_filter_drain_buffer(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, size_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_drain_buffer: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_listener_filter_get_remote_address(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*,
    uint32_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_get_remote_address: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_listener_filter_get_direct_remote_address(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*,
    uint32_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_get_direct_remote_address: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_listener_filter_get_local_address(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*,
    uint32_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_get_local_address: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_listener_filter_get_direct_local_address(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*,
    uint32_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_get_direct_local_address: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) envoy_dynamic_module_type_listener_filter_scheduler_module_ptr
envoy_dynamic_module_callback_listener_filter_scheduler_new(
    envoy_dynamic_module_type_listener_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_scheduler_new: "
               "not implemented in this context");
  return nullptr;
}

__attribute__((weak)) void envoy_dynamic_module_callback_listener_filter_scheduler_commit(
    envoy_dynamic_module_type_listener_filter_scheduler_module_ptr, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_scheduler_commit: "
               "not implemented in this context");
}

__attribute__((weak)) void envoy_dynamic_module_callback_listener_filter_scheduler_delete(
    envoy_dynamic_module_type_listener_filter_scheduler_module_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_scheduler_delete: "
               "not implemented in this context");
}

__attribute__((weak)) envoy_dynamic_module_type_listener_filter_config_scheduler_module_ptr
envoy_dynamic_module_callback_listener_filter_config_scheduler_new(
    envoy_dynamic_module_type_listener_filter_config_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_config_scheduler_new: "
               "not implemented in this context");
  return nullptr;
}

__attribute__((weak)) void envoy_dynamic_module_callback_listener_filter_config_scheduler_delete(
    envoy_dynamic_module_type_listener_filter_config_scheduler_module_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_config_scheduler_delete: "
               "not implemented in this context");
}

__attribute__((weak)) void envoy_dynamic_module_callback_listener_filter_config_scheduler_commit(
    envoy_dynamic_module_type_listener_filter_config_scheduler_module_ptr, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_config_scheduler_commit: "
               "not implemented in this context");
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_listener_filter_config_define_counter(
    envoy_dynamic_module_type_listener_filter_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer, size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_config_define_counter: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_listener_filter_config_define_gauge(
    envoy_dynamic_module_type_listener_filter_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer, size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_config_define_gauge: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_listener_filter_config_define_histogram(
    envoy_dynamic_module_type_listener_filter_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer, size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_config_define_histogram: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

// ============================================================
// Auto-generated weak stubs for filter types not compiled in
// ============================================================

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_access_logger_config_define_counter(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer, size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_config_define_counter: not implemented "
               "in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_access_logger_config_define_gauge(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer, size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_config_define_gauge: not implemented "
               "in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_access_logger_config_define_histogram(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer, size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_config_define_histogram: not "
               "implemented in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_access_logger_decrement_gauge(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_decrement_gauge: not implemented in "
               "this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) uint32_t envoy_dynamic_module_callback_access_logger_get_attempt_count(
    envoy_dynamic_module_type_access_logger_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_attempt_count: not implemented in "
               "this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_attribute_bool(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_attribute_id,
    bool*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_attribute_bool: not implemented in "
               "this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_attribute_int(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_attribute_id,
    uint64_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_attribute_int: not implemented in "
               "this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_attribute_string(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_attribute_id,
    envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG(
      "envoy_dynamic_module_callback_access_logger_get_attribute_string: not implemented in "
      "this context");
  return false;
}

__attribute__((weak)) void envoy_dynamic_module_callback_access_logger_get_bytes_info(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_bytes_info*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_bytes_info: not implemented in "
               "this context");
}

__attribute__((weak)) uint64_t envoy_dynamic_module_callback_access_logger_get_connection_id(
    envoy_dynamic_module_type_access_logger_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_connection_id: not implemented in "
               "this context");
  return 0;
}

__attribute__((weak)) bool
envoy_dynamic_module_callback_access_logger_get_connection_termination_details(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_connection_termination_details: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool
envoy_dynamic_module_callback_access_logger_get_downstream_direct_local_address(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*,
    uint32_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_downstream_direct_local_address: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool
envoy_dynamic_module_callback_access_logger_get_downstream_direct_remote_address(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*,
    uint32_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_downstream_direct_remote_address: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_downstream_local_address(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*,
    uint32_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_downstream_local_address: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_downstream_local_dns_san(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_downstream_local_dns_san: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) size_t
envoy_dynamic_module_callback_access_logger_get_downstream_local_dns_san_size(
    envoy_dynamic_module_type_access_logger_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_downstream_local_dns_san_size: not "
               "implemented in this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_downstream_local_subject(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_downstream_local_subject: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_downstream_local_uri_san(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_downstream_local_uri_san: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) size_t
envoy_dynamic_module_callback_access_logger_get_downstream_local_uri_san_size(
    envoy_dynamic_module_type_access_logger_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_downstream_local_uri_san_size: not "
               "implemented in this context");
  return 0;
}

__attribute__((weak)) bool
envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_digest(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_digest: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) bool
envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_presented(
    envoy_dynamic_module_type_access_logger_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_presented: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) int64_t
envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_v_end(
    envoy_dynamic_module_type_access_logger_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_v_end: not "
               "implemented in this context");
  return 0;
}

__attribute__((weak)) int64_t
envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_v_start(
    envoy_dynamic_module_type_access_logger_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_v_start: not "
               "implemented in this context");
  return 0;
}

__attribute__((weak)) bool
envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_validated(
    envoy_dynamic_module_type_access_logger_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_downstream_peer_cert_validated: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_downstream_peer_dns_san(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_downstream_peer_dns_san: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) size_t
envoy_dynamic_module_callback_access_logger_get_downstream_peer_dns_san_size(
    envoy_dynamic_module_type_access_logger_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_downstream_peer_dns_san_size: not "
               "implemented in this context");
  return 0;
}

__attribute__((weak)) bool
envoy_dynamic_module_callback_access_logger_get_downstream_peer_fingerprint_1(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_downstream_peer_fingerprint_1: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_downstream_peer_issuer(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_downstream_peer_issuer: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_downstream_peer_serial(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_downstream_peer_serial: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_downstream_peer_subject(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_downstream_peer_subject: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_downstream_peer_uri_san(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_downstream_peer_uri_san: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) size_t
envoy_dynamic_module_callback_access_logger_get_downstream_peer_uri_san_size(
    envoy_dynamic_module_type_access_logger_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_downstream_peer_uri_san_size: not "
               "implemented in this context");
  return 0;
}

__attribute__((weak)) bool
envoy_dynamic_module_callback_access_logger_get_downstream_remote_address(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*,
    uint32_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_downstream_remote_address: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_downstream_tls_cipher(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_downstream_tls_cipher: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) bool
envoy_dynamic_module_callback_access_logger_get_downstream_tls_session_id(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_downstream_tls_session_id: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_downstream_tls_version(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_downstream_tls_version: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) bool
envoy_dynamic_module_callback_access_logger_get_downstream_transport_failure_reason(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_downstream_transport_failure_"
               "reason: not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_dynamic_metadata(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_dynamic_metadata: not implemented "
               "in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_filter_state(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_filter_state: not implemented in "
               "this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_header_value(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_http_header_type,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_envoy_buffer*, size_t,
    size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_header_value: not implemented in "
               "this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_headers(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_http_header_type,
    envoy_dynamic_module_type_envoy_http_header*) {
  IS_ENVOY_BUG(
      "envoy_dynamic_module_callback_access_logger_get_headers: not implemented in this context");
  return false;
}

__attribute__((weak)) size_t envoy_dynamic_module_callback_access_logger_get_headers_size(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_http_header_type) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_headers_size: not implemented in "
               "this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_ja3_hash(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG(
      "envoy_dynamic_module_callback_access_logger_get_ja3_hash: not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_ja4_hash(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG(
      "envoy_dynamic_module_callback_access_logger_get_ja4_hash: not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_local_reply_body(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_local_reply_body: not implemented "
               "in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_protocol(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG(
      "envoy_dynamic_module_callback_access_logger_get_protocol: not implemented in this context");
  return false;
}

__attribute__((weak)) uint64_t
envoy_dynamic_module_callback_access_logger_get_request_headers_bytes(
    envoy_dynamic_module_type_access_logger_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_request_headers_bytes: not "
               "implemented in this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_request_id(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_request_id: not implemented in "
               "this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_requested_server_name(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_requested_server_name: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) uint32_t envoy_dynamic_module_callback_access_logger_get_response_code(
    envoy_dynamic_module_type_access_logger_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_response_code: not implemented in "
               "this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_response_code_details(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_response_code_details: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) uint64_t envoy_dynamic_module_callback_access_logger_get_response_flags(
    envoy_dynamic_module_type_access_logger_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_response_flags: not implemented in "
               "this context");
  return 0;
}

__attribute__((weak)) uint64_t
envoy_dynamic_module_callback_access_logger_get_response_headers_bytes(
    envoy_dynamic_module_type_access_logger_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_response_headers_bytes: not "
               "implemented in this context");
  return 0;
}

__attribute__((weak)) uint64_t
envoy_dynamic_module_callback_access_logger_get_response_trailers_bytes(
    envoy_dynamic_module_type_access_logger_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_response_trailers_bytes: not "
               "implemented in this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_route_name(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_route_name: not implemented in "
               "this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_span_id(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG(
      "envoy_dynamic_module_callback_access_logger_get_span_id: not implemented in this context");
  return false;
}

__attribute__((weak)) void envoy_dynamic_module_callback_access_logger_get_timing_info(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_timing_info*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_timing_info: not implemented in "
               "this context");
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_trace_id(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG(
      "envoy_dynamic_module_callback_access_logger_get_trace_id: not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_upstream_cluster(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_upstream_cluster: not implemented "
               "in this context");
  return false;
}

__attribute__((weak)) uint64_t
envoy_dynamic_module_callback_access_logger_get_upstream_connection_id(
    envoy_dynamic_module_type_access_logger_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_upstream_connection_id: not "
               "implemented in this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_upstream_host(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_upstream_host: not implemented in "
               "this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_upstream_local_address(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*,
    uint32_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_upstream_local_address: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_upstream_local_dns_san(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_upstream_local_dns_san: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) size_t
envoy_dynamic_module_callback_access_logger_get_upstream_local_dns_san_size(
    envoy_dynamic_module_type_access_logger_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_upstream_local_dns_san_size: not "
               "implemented in this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_upstream_local_subject(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_upstream_local_subject: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_upstream_local_uri_san(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_upstream_local_uri_san: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) size_t
envoy_dynamic_module_callback_access_logger_get_upstream_local_uri_san_size(
    envoy_dynamic_module_type_access_logger_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_upstream_local_uri_san_size: not "
               "implemented in this context");
  return 0;
}

__attribute__((weak)) bool
envoy_dynamic_module_callback_access_logger_get_upstream_peer_cert_digest(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_upstream_peer_cert_digest: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) int64_t
envoy_dynamic_module_callback_access_logger_get_upstream_peer_cert_v_end(
    envoy_dynamic_module_type_access_logger_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_upstream_peer_cert_v_end: not "
               "implemented in this context");
  return 0;
}

__attribute__((weak)) int64_t
envoy_dynamic_module_callback_access_logger_get_upstream_peer_cert_v_start(
    envoy_dynamic_module_type_access_logger_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_upstream_peer_cert_v_start: not "
               "implemented in this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_upstream_peer_dns_san(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_upstream_peer_dns_san: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) size_t
envoy_dynamic_module_callback_access_logger_get_upstream_peer_dns_san_size(
    envoy_dynamic_module_type_access_logger_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_upstream_peer_dns_san_size: not "
               "implemented in this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_upstream_peer_issuer(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_upstream_peer_issuer: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_upstream_peer_subject(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_upstream_peer_subject: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_upstream_peer_uri_san(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_upstream_peer_uri_san: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) size_t
envoy_dynamic_module_callback_access_logger_get_upstream_peer_uri_san_size(
    envoy_dynamic_module_type_access_logger_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_upstream_peer_uri_san_size: not "
               "implemented in this context");
  return 0;
}

__attribute__((weak)) int64_t
envoy_dynamic_module_callback_access_logger_get_upstream_pool_ready_duration_ns(
    envoy_dynamic_module_type_access_logger_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_upstream_pool_ready_duration_ns: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_upstream_protocol(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_upstream_protocol: not implemented "
               "in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_upstream_remote_address(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*,
    uint32_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_upstream_remote_address: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_upstream_tls_cipher(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_upstream_tls_cipher: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_upstream_tls_session_id(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_upstream_tls_session_id: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_upstream_tls_version(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_upstream_tls_version: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) bool
envoy_dynamic_module_callback_access_logger_get_upstream_transport_failure_reason(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_upstream_transport_failure_reason: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_get_virtual_cluster_name(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_virtual_cluster_name: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) uint32_t envoy_dynamic_module_callback_access_logger_get_worker_index(
    envoy_dynamic_module_type_access_logger_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_get_worker_index: not implemented in "
               "this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_has_response_flag(
    envoy_dynamic_module_type_access_logger_envoy_ptr, envoy_dynamic_module_type_response_flag) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_has_response_flag: not implemented in "
               "this context");
  return false;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_access_logger_increment_counter(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_increment_counter: not implemented in "
               "this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_access_logger_increment_gauge(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_increment_gauge: not implemented in "
               "this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_is_health_check(
    envoy_dynamic_module_type_access_logger_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_is_health_check: not implemented in "
               "this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_is_mtls(
    envoy_dynamic_module_type_access_logger_envoy_ptr) {
  IS_ENVOY_BUG(
      "envoy_dynamic_module_callback_access_logger_is_mtls: not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_access_logger_is_trace_sampled(
    envoy_dynamic_module_type_access_logger_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_is_trace_sampled: not implemented in "
               "this context");
  return false;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_access_logger_record_histogram_value(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_access_logger_record_histogram_value: not "
               "implemented in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_access_logger_set_gauge(
    envoy_dynamic_module_type_access_logger_config_envoy_ptr, size_t, uint64_t) {
  IS_ENVOY_BUG(
      "envoy_dynamic_module_callback_access_logger_set_gauge: not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) void envoy_dynamic_module_callback_listener_filter_close_socket(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_close_socket: not implemented in "
               "this context");
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_listener_filter_decrement_gauge(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_decrement_gauge: not implemented in "
               "this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) envoy_dynamic_module_type_address_type
envoy_dynamic_module_callback_listener_filter_get_address_type(
    envoy_dynamic_module_type_listener_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_get_address_type: not implemented in "
               "this context");
  return envoy_dynamic_module_type_address_type_Unknown;
}

__attribute__((weak)) uint64_t
envoy_dynamic_module_callback_listener_filter_get_connection_start_time_ms(
    envoy_dynamic_module_type_listener_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_get_connection_start_time_ms: not "
               "implemented in this context");
  return 0;
}

__attribute__((weak)) bool
envoy_dynamic_module_callback_listener_filter_get_detected_transport_protocol(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_get_detected_transport_protocol: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) bool
envoy_dynamic_module_callback_listener_filter_get_dynamic_metadata_string(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_get_dynamic_metadata_string: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_listener_filter_get_ja3_hash(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_get_ja3_hash: not implemented in "
               "this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_listener_filter_get_ja4_hash(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_get_ja4_hash: not implemented in "
               "this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_listener_filter_get_original_dst(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*,
    uint32_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_get_original_dst: not implemented in "
               "this context");
  return false;
}

__attribute__((weak)) bool
envoy_dynamic_module_callback_listener_filter_get_requested_application_protocols(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_get_requested_application_protocols: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) size_t
envoy_dynamic_module_callback_listener_filter_get_requested_application_protocols_size(
    envoy_dynamic_module_type_listener_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_get_requested_application_protocols_"
               "size: not implemented in this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_listener_filter_get_requested_server_name(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_get_requested_server_name: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) int64_t envoy_dynamic_module_callback_listener_filter_get_socket_fd(
    envoy_dynamic_module_type_listener_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_get_socket_fd: not implemented in "
               "this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_listener_filter_get_socket_option_bytes(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, int64_t, int64_t, char*, size_t, size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_get_socket_option_bytes: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_listener_filter_get_socket_option_int(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, int64_t, int64_t, int64_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_get_socket_option_int: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_listener_filter_get_ssl_dns_sans(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_get_ssl_dns_sans: not implemented in "
               "this context");
  return false;
}

__attribute__((weak)) size_t envoy_dynamic_module_callback_listener_filter_get_ssl_dns_sans_size(
    envoy_dynamic_module_type_listener_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_get_ssl_dns_sans_size: not "
               "implemented in this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_listener_filter_get_ssl_subject(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_get_ssl_subject: not implemented in "
               "this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_listener_filter_get_ssl_uri_sans(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_get_ssl_uri_sans: not implemented in "
               "this context");
  return false;
}

__attribute__((weak)) size_t envoy_dynamic_module_callback_listener_filter_get_ssl_uri_sans_size(
    envoy_dynamic_module_type_listener_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_get_ssl_uri_sans_size: not "
               "implemented in this context");
  return 0;
}

__attribute__((weak)) uint32_t envoy_dynamic_module_callback_listener_filter_get_worker_index(
    envoy_dynamic_module_type_listener_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_get_worker_index: not implemented in "
               "this context");
  return 0;
}

__attribute__((weak)) envoy_dynamic_module_type_http_callout_init_result
envoy_dynamic_module_callback_listener_filter_http_callout(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, uint64_t*,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_http_header*, size_t,
    envoy_dynamic_module_type_module_buffer, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_http_callout: not implemented in "
               "this context");
  return envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_listener_filter_increment_counter(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_increment_counter: not implemented "
               "in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_listener_filter_increment_gauge(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_increment_gauge: not implemented in "
               "this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_listener_filter_is_local_address_restored(
    envoy_dynamic_module_type_listener_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_is_local_address_restored: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_listener_filter_is_ssl(
    envoy_dynamic_module_type_listener_filter_envoy_ptr) {
  IS_ENVOY_BUG(
      "envoy_dynamic_module_callback_listener_filter_is_ssl: not implemented in this context");
  return false;
}

__attribute__((weak)) size_t envoy_dynamic_module_callback_listener_filter_max_read_bytes(
    envoy_dynamic_module_type_listener_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_max_read_bytes: not implemented in "
               "this context");
  return 0;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_listener_filter_record_histogram_value(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_record_histogram_value: not "
               "implemented in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) void
envoy_dynamic_module_callback_listener_filter_set_downstream_transport_failure_reason(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_set_downstream_transport_failure_"
               "reason: not implemented in this context");
}

__attribute__((weak)) void
envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata_string(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata_string: not "
               "implemented in this context");
}

__attribute__((weak)) bool
envoy_dynamic_module_callback_listener_filter_get_dynamic_metadata_number(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_module_buffer, double*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_get_dynamic_metadata_number: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) void
envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata_number(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_module_buffer, double) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_set_dynamic_metadata_number: not "
               "implemented in this context");
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_listener_filter_set_gauge(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, size_t, uint64_t) {
  IS_ENVOY_BUG(
      "envoy_dynamic_module_callback_listener_filter_set_gauge: not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_listener_filter_set_socket_option_bytes(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, int64_t, int64_t,
    envoy_dynamic_module_type_module_buffer) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_set_socket_option_bytes: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_listener_filter_set_socket_option_int(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, int64_t, int64_t, int64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_set_socket_option_int: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) void envoy_dynamic_module_callback_listener_filter_use_original_dst(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, bool) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_use_original_dst: not implemented in "
               "this context");
}

__attribute__((weak)) int64_t envoy_dynamic_module_callback_listener_filter_write_to_socket(
    envoy_dynamic_module_type_listener_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_listener_filter_write_to_socket: not implemented in "
               "this context");
  return 0;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_listener_filter_config_define_counter(
    envoy_dynamic_module_type_udp_listener_filter_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer, size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_udp_listener_filter_config_define_counter: not "
               "implemented in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_listener_filter_config_define_gauge(
    envoy_dynamic_module_type_udp_listener_filter_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer, size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_udp_listener_filter_config_define_gauge: not "
               "implemented in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_listener_filter_config_define_histogram(
    envoy_dynamic_module_type_udp_listener_filter_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer, size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_udp_listener_filter_config_define_histogram: not "
               "implemented in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_listener_filter_decrement_gauge(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_udp_listener_filter_decrement_gauge: not implemented "
               "in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) bool
envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) size_t
envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks_size(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_chunks_size: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) size_t
envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_size(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_udp_listener_filter_get_datagram_data_size: not "
               "implemented in this context");
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_udp_listener_filter_get_local_address(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer*, uint32_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_udp_listener_filter_get_local_address: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_udp_listener_filter_get_peer_address(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer*, uint32_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_udp_listener_filter_get_peer_address: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) uint32_t envoy_dynamic_module_callback_udp_listener_filter_get_worker_index(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_udp_listener_filter_get_worker_index: not "
               "implemented in this context");
  return 0;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_listener_filter_increment_counter(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_udp_listener_filter_increment_counter: not "
               "implemented in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_listener_filter_increment_gauge(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_udp_listener_filter_increment_gauge: not implemented "
               "in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_listener_filter_record_histogram_value(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_udp_listener_filter_record_histogram_value: not "
               "implemented in this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_udp_listener_filter_send_datagram(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer, uint32_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_udp_listener_filter_send_datagram: not implemented "
               "in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_udp_listener_filter_set_datagram_data(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr,
    envoy_dynamic_module_type_module_buffer) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_udp_listener_filter_set_datagram_data: not "
               "implemented in this context");
  return false;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_udp_listener_filter_set_gauge(
    envoy_dynamic_module_type_udp_listener_filter_envoy_ptr, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_udp_listener_filter_set_gauge: not implemented in "
               "this context");
  return envoy_dynamic_module_type_metrics_result_Success;
}

// ---------------------- Upstream HTTP TCP Bridge callbacks ------------------------
// These are weak symbols that provide default stub implementations. The actual implementations
// are provided in the upstream bridge abi_impl.cc when the upstream bridge extension is used.

__attribute__((weak)) bool
envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_header(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_envoy_buffer*, size_t,
    size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_header: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) size_t
envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_headers_size(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_headers_size: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) bool
envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_headers(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
    envoy_dynamic_module_type_envoy_http_header*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_headers: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) void
envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_buffer(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer*, size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_request_buffer: "
               "not implemented in this context");
}

__attribute__((weak)) void
envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_response_buffer(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer*, size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_upstream_http_tcp_bridge_get_response_buffer: "
               "not implemented in this context");
}

__attribute__((weak)) void
envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_upstream_data(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
    envoy_dynamic_module_type_module_buffer, bool) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_upstream_data: "
               "not implemented in this context");
}

__attribute__((weak)) void envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr, uint32_t,
    envoy_dynamic_module_type_module_http_header*, size_t,
    envoy_dynamic_module_type_module_buffer) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response: "
               "not implemented in this context");
}

__attribute__((weak)) void
envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_headers(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr, uint32_t,
    envoy_dynamic_module_type_module_http_header*, size_t, bool) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_headers: "
               "not implemented in this context");
}

__attribute__((weak)) void
envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_data(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
    envoy_dynamic_module_type_module_buffer, bool) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_data: "
               "not implemented in this context");
}

__attribute__((weak)) void
envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_trailers(
    envoy_dynamic_module_type_upstream_http_tcp_bridge_envoy_ptr,
    envoy_dynamic_module_type_module_http_header*, size_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_upstream_http_tcp_bridge_send_response_trailers: "
               "not implemented in this context");
}

// ---------------------- Tracer callbacks ------------------------
// These are weak symbols that provide default stub implementations. The actual implementations
// are provided in the tracer abi_impl.cc when the tracer extension is used.

__attribute__((weak)) bool envoy_dynamic_module_callback_tracer_get_trace_context_value(
    envoy_dynamic_module_type_tracer_span_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_tracer_get_trace_context_value: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) void envoy_dynamic_module_callback_tracer_set_trace_context_value(
    envoy_dynamic_module_type_tracer_span_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_module_buffer) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_tracer_set_trace_context_value: "
               "not implemented in this context");
}

__attribute__((weak)) void envoy_dynamic_module_callback_tracer_remove_trace_context_value(
    envoy_dynamic_module_type_tracer_span_envoy_ptr, envoy_dynamic_module_type_module_buffer) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_tracer_remove_trace_context_value: "
               "not implemented in this context");
}

__attribute__((weak)) bool envoy_dynamic_module_callback_tracer_get_trace_context_protocol(
    envoy_dynamic_module_type_tracer_span_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_tracer_get_trace_context_protocol: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_tracer_get_trace_context_host(
    envoy_dynamic_module_type_tracer_span_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_tracer_get_trace_context_host: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_tracer_get_trace_context_path(
    envoy_dynamic_module_type_tracer_span_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_tracer_get_trace_context_path: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_tracer_get_trace_context_method(
    envoy_dynamic_module_type_tracer_span_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_tracer_get_trace_context_method: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_tracer_define_counter(
    envoy_dynamic_module_type_tracer_config_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_module_buffer*, size_t, size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_tracer_define_counter: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_tracer_define_gauge(envoy_dynamic_module_type_tracer_config_envoy_ptr,
                                                  envoy_dynamic_module_type_module_buffer,
                                                  envoy_dynamic_module_type_module_buffer*, size_t,
                                                  size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_tracer_define_gauge: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_tracer_define_histogram(
    envoy_dynamic_module_type_tracer_config_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_module_buffer*, size_t, size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_tracer_define_histogram: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_tracer_increment_counter(
    envoy_dynamic_module_type_tracer_config_envoy_ptr, size_t,
    envoy_dynamic_module_type_module_buffer*, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_tracer_increment_counter: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_tracer_record_histogram_value(
    envoy_dynamic_module_type_tracer_config_envoy_ptr, size_t,
    envoy_dynamic_module_type_module_buffer*, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_tracer_record_histogram_value: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_tracer_set_gauge(envoy_dynamic_module_type_tracer_config_envoy_ptr,
                                               size_t, envoy_dynamic_module_type_module_buffer*,
                                               size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_tracer_set_gauge: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_http_add_dynamic_metadata_list_number(
    envoy_dynamic_module_type_http_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_module_buffer, double) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_http_add_dynamic_metadata_list_number: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_http_add_dynamic_metadata_list_string(
    envoy_dynamic_module_type_http_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_http_add_dynamic_metadata_list_string: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_http_add_dynamic_metadata_list_bool(
    envoy_dynamic_module_type_http_filter_envoy_ptr, envoy_dynamic_module_type_module_buffer,
    envoy_dynamic_module_type_module_buffer, bool) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_http_add_dynamic_metadata_list_bool: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_http_get_metadata_list_size(
    envoy_dynamic_module_type_http_filter_envoy_ptr, envoy_dynamic_module_type_metadata_source,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer, size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_http_get_metadata_list_size: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_http_get_metadata_list_number(
    envoy_dynamic_module_type_http_filter_envoy_ptr, envoy_dynamic_module_type_metadata_source,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer, size_t,
    double*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_http_get_metadata_list_number: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_http_get_metadata_list_string(
    envoy_dynamic_module_type_http_filter_envoy_ptr, envoy_dynamic_module_type_metadata_source,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer, size_t,
    envoy_dynamic_module_type_envoy_buffer*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_http_get_metadata_list_string: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_http_get_metadata_list_bool(
    envoy_dynamic_module_type_http_filter_envoy_ptr, envoy_dynamic_module_type_metadata_source,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer, size_t,
    bool*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_http_get_metadata_list_bool: "
               "not implemented in this context");
  return false;
}

// DNS resolver callbacks.
__attribute__((weak)) void envoy_dynamic_module_callback_dns_resolve_complete(
    envoy_dynamic_module_type_dns_resolver_envoy_ptr, uint64_t,
    envoy_dynamic_module_type_dns_resolution_status, envoy_dynamic_module_type_module_buffer,
    const envoy_dynamic_module_type_dns_address*, size_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_dns_resolve_complete: "
               "not implemented in this context");
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_dns_resolver_config_define_counter(
    envoy_dynamic_module_type_dns_resolver_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer*, size_t,
    size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_dns_resolver_config_define_counter: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_dns_resolver_config_increment_counter(
    envoy_dynamic_module_type_dns_resolver_config_envoy_ptr, size_t,
    envoy_dynamic_module_type_module_buffer*, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_dns_resolver_config_increment_counter: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_dns_resolver_config_define_gauge(
    envoy_dynamic_module_type_dns_resolver_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer*, size_t,
    size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_dns_resolver_config_define_gauge: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_dns_resolver_config_set_gauge(
    envoy_dynamic_module_type_dns_resolver_config_envoy_ptr, size_t,
    envoy_dynamic_module_type_module_buffer*, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_dns_resolver_config_set_gauge: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_dns_resolver_config_increment_gauge(
    envoy_dynamic_module_type_dns_resolver_config_envoy_ptr, size_t,
    envoy_dynamic_module_type_module_buffer*, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_dns_resolver_config_increment_gauge: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_dns_resolver_config_decrement_gauge(
    envoy_dynamic_module_type_dns_resolver_config_envoy_ptr, size_t,
    envoy_dynamic_module_type_module_buffer*, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_dns_resolver_config_decrement_gauge: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_dns_resolver_config_define_histogram(
    envoy_dynamic_module_type_dns_resolver_config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer, envoy_dynamic_module_type_module_buffer*, size_t,
    size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_dns_resolver_config_define_histogram: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

__attribute__((weak)) envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_dns_resolver_config_record_histogram_value(
    envoy_dynamic_module_type_dns_resolver_config_envoy_ptr, size_t,
    envoy_dynamic_module_type_module_buffer*, size_t, uint64_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_dns_resolver_config_record_histogram_value: "
               "not implemented in this context");
  return envoy_dynamic_module_type_metrics_result_MetricNotFound;
}

// Transport socket callbacks.
__attribute__((weak)) void* envoy_dynamic_module_callback_transport_socket_get_io_handle(
    envoy_dynamic_module_type_transport_socket_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_transport_socket_get_io_handle: "
               "not implemented in this context");
  return nullptr;
}

__attribute__((weak)) int64_t
envoy_dynamic_module_callback_transport_socket_io_handle_read(void*, char*, size_t, size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_transport_socket_io_handle_read: "
               "not implemented in this context");
  return -1;
}

__attribute__((weak)) int64_t envoy_dynamic_module_callback_transport_socket_io_handle_write(
    void*, const char*, size_t, size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_transport_socket_io_handle_write: "
               "not implemented in this context");
  return -1;
}

__attribute__((weak)) int envoy_dynamic_module_callback_transport_socket_io_handle_fd(void*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_transport_socket_io_handle_fd: "
               "not implemented in this context");
  return -1;
}

__attribute__((weak)) void envoy_dynamic_module_callback_transport_socket_read_buffer_drain(
    envoy_dynamic_module_type_transport_socket_envoy_ptr, size_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_transport_socket_read_buffer_drain: "
               "not implemented in this context");
}

__attribute__((weak)) void envoy_dynamic_module_callback_transport_socket_read_buffer_add(
    envoy_dynamic_module_type_transport_socket_envoy_ptr, const char*, size_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_transport_socket_read_buffer_add: "
               "not implemented in this context");
}

__attribute__((weak)) size_t envoy_dynamic_module_callback_transport_socket_read_buffer_length(
    envoy_dynamic_module_type_transport_socket_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_transport_socket_read_buffer_length: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) void envoy_dynamic_module_callback_transport_socket_write_buffer_drain(
    envoy_dynamic_module_type_transport_socket_envoy_ptr, size_t) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_transport_socket_write_buffer_drain: "
               "not implemented in this context");
}

__attribute__((weak)) void envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(
    envoy_dynamic_module_type_transport_socket_envoy_ptr, envoy_dynamic_module_type_envoy_buffer*,
    size_t*) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices: "
               "not implemented in this context");
}

__attribute__((weak)) size_t envoy_dynamic_module_callback_transport_socket_write_buffer_length(
    envoy_dynamic_module_type_transport_socket_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_transport_socket_write_buffer_length: "
               "not implemented in this context");
  return 0;
}

__attribute__((weak)) void envoy_dynamic_module_callback_transport_socket_raise_event(
    envoy_dynamic_module_type_transport_socket_envoy_ptr,
    envoy_dynamic_module_type_network_connection_event) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_transport_socket_raise_event: "
               "not implemented in this context");
}

__attribute__((weak)) bool envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer(
    envoy_dynamic_module_type_transport_socket_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer: "
               "not implemented in this context");
  return false;
}

__attribute__((weak)) void envoy_dynamic_module_callback_transport_socket_set_is_readable(
    envoy_dynamic_module_type_transport_socket_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_transport_socket_set_is_readable: "
               "not implemented in this context");
}

__attribute__((weak)) void envoy_dynamic_module_callback_transport_socket_flush_write_buffer(
    envoy_dynamic_module_type_transport_socket_envoy_ptr) {
  IS_ENVOY_BUG("envoy_dynamic_module_callback_transport_socket_flush_write_buffer: "
               "not implemented in this context");
}

} // extern "C"
