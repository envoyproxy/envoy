#include <stdint.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// This module records the user data the x-user-data request header carries, if any, then ends the
// decision the way the x-outcome request header names, so that a test can observe which recorded
// values Envoy hands back through the user data discard hook and which reach the route destroy
// hook.

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

static int route_destroy_count = 0;
static int user_data_discard_count = 0;
static uint64_t last_discarded_user_data = 0;

// Let tests observe which hooks Envoy invoked and with which value.
int getRouteDestroyCount(void) { return route_destroy_count; }
int getUserDataDiscardCount(void) { return user_data_discard_count; }
uint64_t getLastDiscardedUserData(void) { return last_discarded_user_data; }

envoy_dynamic_module_type_route_specifier_config_module_ptr
envoy_dynamic_module_on_route_specifier_config_new(
    envoy_dynamic_module_type_route_specifier_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  static int config_dummy = 0;
  return &config_dummy;
}

void envoy_dynamic_module_on_route_specifier_config_destroy(
    envoy_dynamic_module_type_route_specifier_config_module_ptr config_module_ptr) {}

void envoy_dynamic_module_on_route_specifier_route_destroy(
    envoy_dynamic_module_type_route_specifier_config_module_ptr config_module_ptr,
    uint64_t user_data) {
  route_destroy_count++;
}

void envoy_dynamic_module_on_route_specifier_user_data_discard(
    envoy_dynamic_module_type_route_specifier_config_module_ptr config_module_ptr,
    uint64_t user_data) {
  user_data_discard_count++;
  last_discarded_user_data = user_data;
}

static envoy_dynamic_module_type_module_buffer buffer(const char* value) {
  envoy_dynamic_module_type_module_buffer result = {value, strlen(value)};
  return result;
}

// The value of a request header, or an empty buffer when the request does not carry it.
static envoy_dynamic_module_type_envoy_buffer
header(envoy_dynamic_module_type_route_specifier_context_envoy_ptr ctx, const char* name) {
  envoy_dynamic_module_type_envoy_buffer value = {NULL, 0};
  if (!envoy_dynamic_module_callback_route_specifier_get_request_header_value(ctx, buffer(name),
                                                                              &value, 0, NULL)) {
    value.ptr = NULL;
    value.length = 0;
  }
  return value;
}

static uint64_t number(envoy_dynamic_module_type_envoy_buffer value) {
  uint64_t result = 0;
  for (size_t i = 0; i < value.length; i++) {
    result = result * 10 + (uint64_t)(value.ptr[i] - '0');
  }
  return result;
}

static int header_is(envoy_dynamic_module_type_envoy_buffer value, const char* expected) {
  const size_t length = strlen(expected);
  return value.ptr != NULL && value.length == length && memcmp(value.ptr, expected, length) == 0;
}

envoy_dynamic_module_type_route_specifier_on_route_status
envoy_dynamic_module_on_route_specifier_on_route(
    envoy_dynamic_module_type_route_specifier_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr ctx) {
  const envoy_dynamic_module_type_envoy_buffer user_data = header(ctx, "x-user-data");
  if (user_data.ptr != NULL) {
    envoy_dynamic_module_callback_route_specifier_set_route_user_data(ctx, number(user_data));
  }
  const envoy_dynamic_module_type_envoy_buffer outcome = header(ctx, "x-outcome");
  if (header_is(outcome, "template")) {
    envoy_dynamic_module_callback_route_specifier_set_route_template(ctx, buffer("only"));
  } else if (header_is(outcome, "pass-through")) {
    envoy_dynamic_module_callback_route_specifier_set_decision(
        ctx, envoy_dynamic_module_type_route_specifier_decision_PassThrough);
  } else if (header_is(outcome, "recorded-twice")) {
    envoy_dynamic_module_callback_route_specifier_set_route_user_data(ctx, number(user_data) + 1);
    envoy_dynamic_module_callback_route_specifier_set_decision(
        ctx, envoy_dynamic_module_type_route_specifier_decision_PassThrough);
  } else if (header_is(outcome, "no-route")) {
    envoy_dynamic_module_callback_route_specifier_set_decision(
        ctx, envoy_dynamic_module_type_route_specifier_decision_NoRoute);
  } else if (header_is(outcome, "error")) {
    envoy_dynamic_module_callback_route_specifier_set_decision(
        ctx, envoy_dynamic_module_type_route_specifier_decision_Error);
  } else if (header_is(outcome, "reuse-previous")) {
    envoy_dynamic_module_callback_route_specifier_set_decision(
        ctx, envoy_dynamic_module_type_route_specifier_decision_ReusePrevious);
  } else if (header_is(outcome, "unknown-decision")) {
    envoy_dynamic_module_callback_route_specifier_set_decision(
        ctx, (envoy_dynamic_module_type_route_specifier_decision)99);
  } else if (header_is(outcome, "skip")) {
    return envoy_dynamic_module_type_route_specifier_on_route_status_StopIterationAndSkipRoute;
  } else if (header_is(outcome, "unknown-status")) {
    return (envoy_dynamic_module_type_route_specifier_on_route_status)99;
  } else if (header_is(outcome, "missing-template")) {
    envoy_dynamic_module_callback_route_specifier_set_route_template(ctx, buffer("missing"));
  } else if (header_is(outcome, "unmatched-template")) {
    envoy_dynamic_module_callback_route_specifier_set_route_template(ctx, buffer("never"));
  } else if (header_is(outcome, "direct-entry")) {
    envoy_dynamic_module_callback_route_specifier_set_route_template(ctx, buffer("direct"));
    envoy_dynamic_module_callback_route_specifier_set_cluster_name(ctx, buffer("module_cluster"));
  } else if (header_is(outcome, "rejected-metadata")) {
    envoy_dynamic_module_callback_route_specifier_set_route_metadata_string(
        ctx, buffer("envoy.test.route"), buffer("key"), buffer("value"));
  } else if (header_is(outcome, "cached")) {
    // A miss caches the route with the value, and an unverified hit hands its value back.
    envoy_dynamic_module_callback_route_specifier_set_route_template(ctx, buffer("only"));
    envoy_dynamic_module_callback_route_specifier_lookup_built_route(ctx, 1);
  }
  // An outcome not named above refines the route matching resolved.
  return envoy_dynamic_module_type_route_specifier_on_route_status_StopIteration;
}
