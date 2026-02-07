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
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  auto context = Server::Configuration::ServerFactoryContextInstance::getExisting();
  return context->options().concurrency();
}

// ---------------------- Function registry callbacks --------------------------------

bool envoy_dynamic_module_callback_register_function(envoy_dynamic_module_type_module_buffer key,
                                                     void* function_ptr) {
  if (function_ptr == nullptr) {
    return false;
  }
  std::string key_str(key.ptr, key.length);
  absl::WriterMutexLock lock(&function_registry_mutex);
  auto [it, inserted] = function_registry.try_emplace(key_str, function_ptr);
  return inserted;
}

bool envoy_dynamic_module_callback_get_function(envoy_dynamic_module_type_module_buffer key,
                                                void** function_ptr_out) {
  std::string key_str(key.ptr, key.length);
  absl::ReaderMutexLock lock(&function_registry_mutex);
  auto it = function_registry.find(key_str);
  if (it != function_registry.end()) {
    *function_ptr_out = it->second;
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

} // extern "C"
