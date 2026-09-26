#include <string.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// This module records route overrides so that a test can read the delegating route wrappers
// directly. The specifier config selects what is recorded: "route-only" records only route
// metadata, "entry-no-meta" records only a cluster, "filter-only" records only a disabled filter,
// "select-override" applies a route override declared in the configuration, and any other config
// records route entry overrides with metadata. The metadata-free modes let a test reach the wrapper
// metadata accessors when no metadata pack is built.

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

// Distinct addresses used as the config handle that selects the recorded overrides.
static char entry_mode;
static char route_only_mode;
static char entry_no_meta_mode;
static char filter_only_mode;
static char select_override_mode;

static int config_is(envoy_dynamic_module_type_envoy_buffer config, const char* name) {
  const size_t length = strlen(name);
  return config.length == length && memcmp(config.ptr, name, length) == 0;
}

envoy_dynamic_module_type_route_specifier_config_module_ptr
envoy_dynamic_module_on_route_specifier_config_new(
    envoy_dynamic_module_type_route_specifier_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  if (config_is(config, "route-only")) {
    return &route_only_mode;
  }
  if (config_is(config, "entry-no-meta")) {
    return &entry_no_meta_mode;
  }
  if (config_is(config, "filter-only")) {
    return &filter_only_mode;
  }
  if (config_is(config, "select-override")) {
    return &select_override_mode;
  }
  return &entry_mode;
}

void envoy_dynamic_module_on_route_specifier_config_destroy(
    envoy_dynamic_module_type_route_specifier_config_module_ptr config_module_ptr) {}

envoy_dynamic_module_type_route_specifier_decision envoy_dynamic_module_on_route_specifier_on_route(
    envoy_dynamic_module_type_route_specifier_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr) {
  // Selecting a declared route override isolates the route entry override accessors from the other
  // recorded overrides.
  if (config_module_ptr == &select_override_mode) {
    const envoy_dynamic_module_type_module_buffer override_id = {"applied", 7};
    envoy_dynamic_module_callback_route_specifier_set_route_override(context_envoy_ptr,
                                                                     override_id);
    return envoy_dynamic_module_type_route_specifier_decision_Override;
  }
  // The metadata-free modes skip metadata so a test can reach the wrapper metadata fallbacks.
  if (config_module_ptr != &entry_no_meta_mode && config_module_ptr != &filter_only_mode) {
    const envoy_dynamic_module_type_module_buffer ns = {"envoy.test.route", 16};
    const envoy_dynamic_module_type_module_buffer key = {"key", 3};
    const envoy_dynamic_module_type_module_buffer value = {"value", 5};
    envoy_dynamic_module_callback_route_specifier_set_route_metadata_string(context_envoy_ptr, ns,
                                                                            key, value);
  }
  if (config_module_ptr == &route_only_mode) {
    return envoy_dynamic_module_type_route_specifier_decision_Override;
  }
  if (config_module_ptr == &filter_only_mode) {
    const envoy_dynamic_module_type_module_buffer filter = {"envoy.test.disabled", 19};
    envoy_dynamic_module_callback_route_specifier_set_filter_disabled(context_envoy_ptr, filter,
                                                                      true);
    return envoy_dynamic_module_type_route_specifier_decision_Override;
  }
  const envoy_dynamic_module_type_module_buffer cluster = {"canary", 6};
  envoy_dynamic_module_callback_route_specifier_set_cluster_name(context_envoy_ptr, cluster);
  if (config_module_ptr == &entry_no_meta_mode) {
    return envoy_dynamic_module_type_route_specifier_decision_Override;
  }
  const envoy_dynamic_module_type_module_buffer path = {"/rewritten", 10};
  envoy_dynamic_module_callback_route_specifier_set_path(context_envoy_ptr, path);
  // Record the scalar route entry overrides so a test can read them from the wrapper.
  envoy_dynamic_module_callback_route_specifier_set_idle_timeout(context_envoy_ptr, 8000);
  envoy_dynamic_module_callback_route_specifier_set_max_stream_duration(context_envoy_ptr, 9000);
  envoy_dynamic_module_callback_route_specifier_set_request_body_buffer_limit(context_envoy_ptr,
                                                                              4096);
  envoy_dynamic_module_callback_route_specifier_set_priority(
      context_envoy_ptr, envoy_dynamic_module_type_resource_priority_High);
  const envoy_dynamic_module_type_module_buffer value = {"value", 5};
  // Record a request header for each transform arm and a removal, so requestHeaderTransforms
  // exercises every arm of appendHeaderTransforms.
  const envoy_dynamic_module_type_module_buffer append_key = {"x-append", 8};
  envoy_dynamic_module_callback_route_specifier_add_request_header(
      context_envoy_ptr, append_key, value,
      envoy_dynamic_module_type_route_specifier_header_append_action_AppendIfExistsOrAdd);
  const envoy_dynamic_module_type_module_buffer absent_key = {"x-add-if-absent", 15};
  envoy_dynamic_module_callback_route_specifier_add_request_header(
      context_envoy_ptr, absent_key, value,
      envoy_dynamic_module_type_route_specifier_header_append_action_AddIfAbsent);
  const envoy_dynamic_module_type_module_buffer overwrite_key = {"x-overwrite", 11};
  envoy_dynamic_module_callback_route_specifier_add_request_header(
      context_envoy_ptr, overwrite_key, value,
      envoy_dynamic_module_type_route_specifier_header_append_action_OverwriteIfExistsOrAdd);
  const envoy_dynamic_module_type_module_buffer remove_key = {"x-remove", 8};
  envoy_dynamic_module_callback_route_specifier_remove_request_header(context_envoy_ptr,
                                                                      remove_key);
  const envoy_dynamic_module_type_module_buffer response_key = {"x-added", 7};
  envoy_dynamic_module_callback_route_specifier_add_response_header(
      context_envoy_ptr, response_key, value,
      envoy_dynamic_module_type_route_specifier_header_append_action_AddIfAbsent);
  return envoy_dynamic_module_type_route_specifier_decision_Override;
}
