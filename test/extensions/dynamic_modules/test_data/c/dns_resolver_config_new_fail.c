#include "source/extensions/dynamic_modules/abi/abi.h"

// Test stub that exports all DNS resolver ABI symbols but returns null from
// envoy_dynamic_module_on_dns_resolver_config_new so that
// HickoryDnsResolverConfig::createForModule exercises its "module rejected the configuration"
// failure path.
envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_dns_resolver_config_module_ptr
envoy_dynamic_module_on_dns_resolver_config_new(
    envoy_dynamic_module_type_dns_resolver_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  (void)config_envoy_ptr;
  (void)name;
  (void)config;
  // Intentionally return null to simulate the Rust module rejecting the configuration.
  return NULL;
}

void envoy_dynamic_module_on_dns_resolver_config_destroy(
    envoy_dynamic_module_type_dns_resolver_config_module_ptr config_module_ptr) {
  (void)config_module_ptr;
}

envoy_dynamic_module_type_dns_resolver_module_ptr envoy_dynamic_module_on_dns_resolver_new(
    envoy_dynamic_module_type_dns_resolver_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_dns_resolver_envoy_ptr resolver_envoy_ptr) {
  (void)config_module_ptr;
  (void)resolver_envoy_ptr;
  return NULL;
}

void envoy_dynamic_module_on_dns_resolver_destroy(
    envoy_dynamic_module_type_dns_resolver_module_ptr resolver_module_ptr) {
  (void)resolver_module_ptr;
}

envoy_dynamic_module_type_dns_query_module_ptr envoy_dynamic_module_on_dns_resolve(
    envoy_dynamic_module_type_dns_resolver_module_ptr resolver_module_ptr,
    envoy_dynamic_module_type_envoy_buffer dns_name,
    envoy_dynamic_module_type_dns_lookup_family lookup_family, uint64_t query_id) {
  (void)resolver_module_ptr;
  (void)dns_name;
  (void)lookup_family;
  (void)query_id;
  return NULL;
}

void envoy_dynamic_module_on_dns_resolve_cancel(
    envoy_dynamic_module_type_dns_resolver_module_ptr resolver_module_ptr,
    envoy_dynamic_module_type_dns_query_module_ptr query_module_ptr) {
  (void)resolver_module_ptr;
  (void)query_module_ptr;
}

void envoy_dynamic_module_on_dns_resolver_reset_networking(
    envoy_dynamic_module_type_dns_resolver_module_ptr resolver_module_ptr) {
  (void)resolver_module_ptr;
}
