// NOLINT(namespace-envoy)

// This file provides host-side implementations for ABI callbacks that are shared across
// all dynamic modules. These are the "Common Callbacks" declared in abi.h and are available
// regardless of which extension point is being used (HTTP/Network/Listener/UDP/Bootstrap/etc).

#include <atomic>
#include <memory>
#include <string>

#include "envoy/server/factory_context.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/extensions/dynamic_modules/abi/abi.h"

#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"

namespace {

// A single entry in the shared state registry. The data pointer is stored atomically so that
// handles can read it lock-free on the per-request path.
struct SharedStateEntry {
  std::atomic<const void*> data{nullptr};
};

// Process-wide shared state registry. This allows bootstrap extensions to publish
// named opaque pointers that HTTP filters and other extensions can retrieve.
// Entries are stored as shared_ptr so that handles can hold independent references.
absl::Mutex shared_state_mutex;
absl::flat_hash_map<std::string, std::shared_ptr<SharedStateEntry>>
    shared_state_registry ABSL_GUARDED_BY(shared_state_mutex);

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

// ---------------------- Shared state registry callbacks --------------------------------

bool envoy_dynamic_module_callback_publish_shared_state(envoy_dynamic_module_type_module_buffer key,
                                                        const void* data) {
  std::string key_str(key.ptr, key.length);
  absl::WriterMutexLock lock(&shared_state_mutex);
  if (data != nullptr) {
    // Create the entry if it does not exist. Entries are never removed from the map so that
    // outstanding handles remain valid across publish/clear cycles.
    auto [it, inserted] =
        shared_state_registry.try_emplace(key_str, std::make_shared<SharedStateEntry>());
    it->second->data.store(data, std::memory_order_release);
  } else {
    // Clear the data but keep the entry so that existing handles see the update.
    auto it = shared_state_registry.find(key_str);
    if (it != shared_state_registry.end()) {
      it->second->data.store(nullptr, std::memory_order_release);
    }
  }
  return true;
}

bool envoy_dynamic_module_callback_get_shared_state(envoy_dynamic_module_type_module_buffer key,
                                                    const void** data_out) {
  std::string key_str(key.ptr, key.length);
  absl::ReaderMutexLock lock(&shared_state_mutex);
  auto it = shared_state_registry.find(key_str);
  if (it != shared_state_registry.end()) {
    const void* d = it->second->data.load(std::memory_order_acquire);
    if (d != nullptr) {
      *data_out = d;
      return true;
    }
  }
  return false;
}

// ---------------------- Shared state handle callbacks -----------------------------------

envoy_dynamic_module_type_shared_state_handle
envoy_dynamic_module_callback_shared_state_handle_new(envoy_dynamic_module_type_module_buffer key) {
  std::string key_str(key.ptr, key.length);
  absl::ReaderMutexLock lock(&shared_state_mutex);
  auto it = shared_state_registry.find(key_str);
  if (it != shared_state_registry.end()) {
    // Return a heap-allocated shared_ptr that holds a reference to the entry.
    return static_cast<envoy_dynamic_module_type_shared_state_handle>(
        new std::shared_ptr<SharedStateEntry>(it->second));
  }
  return nullptr;
}

bool envoy_dynamic_module_callback_shared_state_handle_get(
    envoy_dynamic_module_type_shared_state_handle handle, const void** data_out) {
  auto* sp = static_cast<std::shared_ptr<SharedStateEntry>*>(handle);
  const void* d = (*sp)->data.load(std::memory_order_acquire);
  if (d != nullptr) {
    *data_out = d;
    return true;
  }
  return false;
}

void envoy_dynamic_module_callback_shared_state_handle_delete(
    envoy_dynamic_module_type_shared_state_handle handle) {
  auto* sp = static_cast<std::shared_ptr<SharedStateEntry>*>(handle);
  delete sp;
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
