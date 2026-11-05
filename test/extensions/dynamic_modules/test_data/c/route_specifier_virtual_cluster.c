#include <stdint.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// This module selects the virtual cluster the x-virtual-cluster request header names, and then the
// one x-later-virtual-cluster names, an empty value selecting none, so that a test can observe how
// Envoy applies the selection. A request that carries x-null-virtual-cluster then selects with a
// null buffer whose length is that of the header value. Before that the module selects the route
// template x-template names and looks the route up in the built route cache by the x-key number,
// recording nothing more on a hit. It records the cluster x-cluster names and the route name
// x-route-name names, and ends the decision the way x-decision names.

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

static char config;
static int last_virtual_cluster_result = -1;
static int config_destroy_count = 0;

// Let tests observe what the last selection returned, -1 when the module made none, and how often
// Envoy destroyed a configuration.
int getLastVirtualClusterResult(void) { return last_virtual_cluster_result; }
int getConfigDestroyCount(void) { return config_destroy_count; }

envoy_dynamic_module_type_route_specifier_config_module_ptr
envoy_dynamic_module_on_route_specifier_config_new(
    envoy_dynamic_module_type_route_specifier_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer value) {
  return &config;
}

void envoy_dynamic_module_on_route_specifier_config_destroy(
    envoy_dynamic_module_type_route_specifier_config_module_ptr config_module_ptr) {
  config_destroy_count++;
}

static envoy_dynamic_module_type_module_buffer buffer(const char* value) {
  envoy_dynamic_module_type_module_buffer result = {value, strlen(value)};
  return result;
}

// Whether the request carries the header, whose value is then written to value.
static int header(envoy_dynamic_module_type_route_specifier_context_envoy_ptr ctx,
                  const char* name, envoy_dynamic_module_type_module_buffer* value) {
  envoy_dynamic_module_type_envoy_buffer found = {NULL, 0};
  if (!envoy_dynamic_module_callback_route_specifier_get_request_header_value(ctx, buffer(name),
                                                                              &found, 0, NULL)) {
    return 0;
  }
  value->ptr = found.ptr;
  value->length = found.length;
  return 1;
}

static uint64_t number(envoy_dynamic_module_type_module_buffer value) {
  uint64_t result = 0;
  for (size_t i = 0; i < value.length; i++) {
    result = result * 10 + (uint64_t)(value.ptr[i] - '0');
  }
  return result;
}

static int is(envoy_dynamic_module_type_module_buffer value, const char* expected) {
  const size_t length = strlen(expected);
  return value.length == length && memcmp(value.ptr, expected, length) == 0;
}

// Selects the virtual cluster the header names when the request carries it.
static void select_virtual_cluster(envoy_dynamic_module_type_route_specifier_context_envoy_ptr ctx,
                                   const char* name) {
  envoy_dynamic_module_type_module_buffer value;
  if (header(ctx, name, &value)) {
    last_virtual_cluster_result =
        envoy_dynamic_module_callback_route_specifier_set_virtual_cluster(ctx, value);
  }
}

envoy_dynamic_module_type_route_specifier_on_route_status
envoy_dynamic_module_on_route_specifier_on_route(
    envoy_dynamic_module_type_route_specifier_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr ctx) {
  const envoy_dynamic_module_type_route_specifier_on_route_status status =
      envoy_dynamic_module_type_route_specifier_on_route_status_Continue;
  envoy_dynamic_module_type_module_buffer value;
  last_virtual_cluster_result = -1;
  if (header(ctx, "x-template", &value)) {
    envoy_dynamic_module_callback_route_specifier_set_route_template(ctx, value);
  }
  if (header(ctx, "x-key", &value) &&
      envoy_dynamic_module_callback_route_specifier_lookup_built_route(ctx, number(value)) ==
          envoy_dynamic_module_type_route_specifier_built_route_lookup_result_Hit) {
    return status;
  }
  select_virtual_cluster(ctx, "x-virtual-cluster");
  select_virtual_cluster(ctx, "x-later-virtual-cluster");
  if (header(ctx, "x-null-virtual-cluster", &value)) {
    const envoy_dynamic_module_type_module_buffer null_name = {NULL, value.length};
    last_virtual_cluster_result =
        envoy_dynamic_module_callback_route_specifier_set_virtual_cluster(ctx, null_name);
  }
  if (header(ctx, "x-cluster", &value)) {
    envoy_dynamic_module_callback_route_specifier_set_cluster_name(ctx, value);
  }
  if (header(ctx, "x-route-name", &value)) {
    envoy_dynamic_module_callback_route_specifier_set_route_name(ctx, value);
  }
  if (header(ctx, "x-decision", &value)) {
    if (is(value, "pass-through")) {
      envoy_dynamic_module_callback_route_specifier_set_decision(
          ctx, envoy_dynamic_module_type_route_specifier_decision_PassThrough);
    } else if (is(value, "reuse-previous")) {
      envoy_dynamic_module_callback_route_specifier_set_decision(
          ctx, envoy_dynamic_module_type_route_specifier_decision_ReusePrevious);
    } else if (is(value, "no-route")) {
      envoy_dynamic_module_callback_route_specifier_set_decision(
          ctx, envoy_dynamic_module_type_route_specifier_decision_NoRoute);
    } else if (is(value, "error")) {
      envoy_dynamic_module_callback_route_specifier_set_decision(
          ctx, envoy_dynamic_module_type_route_specifier_decision_Error);
    }
  }
  return status;
}
