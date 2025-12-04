// Stub implementations for cluster ABI callbacks.
// These are used when the cluster dynamic modules extension is not linked.
// The real implementations are in source/extensions/clusters/dynamic_modules/abi_impl.cc.
// NOLINT(namespace-envoy)

#include "source/extensions/dynamic_modules/abi.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

extern "C" {

// LCOV_EXCL_START
// These are weak symbols that will be overridden by the real implementations
// when the cluster extension is linked. They are never executed in tests.

__attribute__((weak)) size_t envoy_dynamic_module_callback_cluster_get_name(
    envoy_dynamic_module_type_cluster_envoy_ptr, envoy_dynamic_module_type_buffer_envoy_ptr*) {
  return 0;
}

__attribute__((weak)) envoy_dynamic_module_type_cluster_result
envoy_dynamic_module_callback_cluster_add_host(envoy_dynamic_module_type_cluster_envoy_ptr,
                                               envoy_dynamic_module_type_buffer_module_ptr, size_t,
                                               uint32_t, uint32_t,
                                               envoy_dynamic_module_type_host_envoy_ptr*) {
  return envoy_dynamic_module_type_cluster_result_Error;
}

__attribute__((weak)) envoy_dynamic_module_type_cluster_result
envoy_dynamic_module_callback_cluster_remove_host(envoy_dynamic_module_type_cluster_envoy_ptr,
                                                  envoy_dynamic_module_type_host_envoy_ptr) {
  return envoy_dynamic_module_type_cluster_result_Error;
}

__attribute__((weak)) void
envoy_dynamic_module_callback_cluster_get_hosts(envoy_dynamic_module_type_cluster_envoy_ptr,
                                                envoy_dynamic_module_type_host_info** hosts_out,
                                                size_t* hosts_count_out) {
  *hosts_out = nullptr;
  *hosts_count_out = 0;
}

__attribute__((weak)) envoy_dynamic_module_type_host_envoy_ptr
envoy_dynamic_module_callback_cluster_get_host_by_address(
    envoy_dynamic_module_type_cluster_envoy_ptr, envoy_dynamic_module_type_buffer_module_ptr,
    size_t, uint32_t) {
  return nullptr;
}

__attribute__((weak)) void
envoy_dynamic_module_callback_host_set_weight(envoy_dynamic_module_type_host_envoy_ptr, uint32_t) {}

__attribute__((weak)) size_t envoy_dynamic_module_callback_host_get_address(
    envoy_dynamic_module_type_host_envoy_ptr, envoy_dynamic_module_type_buffer_envoy_ptr*,
    uint32_t*) {
  return 0;
}

__attribute__((weak)) envoy_dynamic_module_type_host_health
envoy_dynamic_module_callback_host_get_health(envoy_dynamic_module_type_host_envoy_ptr) {
  return envoy_dynamic_module_type_host_health_Unhealthy;
}

__attribute__((weak)) uint32_t
envoy_dynamic_module_callback_host_get_weight(envoy_dynamic_module_type_host_envoy_ptr) {
  return 0;
}

__attribute__((weak)) void envoy_dynamic_module_callback_cluster_pre_init_complete(
    envoy_dynamic_module_type_cluster_envoy_ptr) {}

__attribute__((weak)) bool envoy_dynamic_module_callback_lb_context_get_hash_key(
    envoy_dynamic_module_type_lb_context_envoy_ptr, uint64_t*) {
  return false;
}

__attribute__((weak)) size_t envoy_dynamic_module_callback_lb_context_get_header(
    envoy_dynamic_module_type_lb_context_envoy_ptr, envoy_dynamic_module_type_buffer_module_ptr,
    size_t, envoy_dynamic_module_type_buffer_envoy_ptr*) {
  return 0;
}

__attribute__((weak)) size_t envoy_dynamic_module_callback_lb_context_get_override_host(
    envoy_dynamic_module_type_lb_context_envoy_ptr, envoy_dynamic_module_type_buffer_envoy_ptr*,
    bool*) {
  return 0;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_lb_context_get_attempt_count(
    envoy_dynamic_module_type_lb_context_envoy_ptr, uint32_t*) {
  return false;
}

__attribute__((weak)) bool envoy_dynamic_module_callback_lb_context_get_downstream_connection_id(
    envoy_dynamic_module_type_lb_context_envoy_ptr, uint64_t*) {
  return false;
}

__attribute__((weak)) envoy_dynamic_module_type_thread_local_cluster_envoy_ptr
envoy_dynamic_module_callback_cluster_manager_get_thread_local_cluster(
    envoy_dynamic_module_type_cluster_envoy_ptr, envoy_dynamic_module_type_buffer_module_ptr,
    size_t) {
  return nullptr;
}

__attribute__((weak)) envoy_dynamic_module_type_host_envoy_ptr
envoy_dynamic_module_callback_thread_local_cluster_choose_host(
    envoy_dynamic_module_type_thread_local_cluster_envoy_ptr,
    envoy_dynamic_module_type_lb_context_envoy_ptr) {
  return nullptr;
}

__attribute__((weak)) size_t envoy_dynamic_module_callback_thread_local_cluster_get_name(
    envoy_dynamic_module_type_thread_local_cluster_envoy_ptr,
    envoy_dynamic_module_type_buffer_envoy_ptr*) {
  return 0;
}

__attribute__((weak)) size_t envoy_dynamic_module_callback_thread_local_cluster_host_count(
    envoy_dynamic_module_type_thread_local_cluster_envoy_ptr) {
  return 0;
}

// LCOV_EXCL_STOP

} // extern "C"

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
