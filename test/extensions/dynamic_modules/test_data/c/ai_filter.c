#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// One AI filter module whose behavior is selected by the filter name, so each test names the
// scenario it exercises. Results the hooks observe are published as filter state for the test to
// read.

typedef enum {
  scenario_passthrough,
  scenario_skip_on_header,
  scenario_fail_new,
  scenario_null_filter,
  scenario_error,
  scenario_local_reply,
  scenario_rewrite,
  scenario_invalid_headers,
  scenario_outside_request,
} scenario;

typedef struct {
  const char* name;
  scenario value;
} scenario_entry;

static const scenario_entry scenarios[] = {
    {"passthrough", scenario_passthrough},
    {"skip_on_header", scenario_skip_on_header},
    {"fail_new", scenario_fail_new},
    {"null_filter", scenario_null_filter},
    {"error", scenario_error},
    {"local_reply", scenario_local_reply},
    {"rewrite", scenario_rewrite},
    {"invalid_headers", scenario_invalid_headers},
    {"outside_request", scenario_outside_request},
};

static int live_configs = 0;
static int live_filters = 0;

// Exported for tests to check that every config and filter was destroyed.
int ai_filter_test_live_configs(void) { return live_configs; }
int ai_filter_test_live_filters(void) { return live_filters; }

static envoy_dynamic_module_type_module_buffer str_buf(const char* s) {
  envoy_dynamic_module_type_module_buffer buf = {s, strlen(s)};
  return buf;
}

static envoy_dynamic_module_type_module_buffer envoy_buf(envoy_dynamic_module_type_envoy_buffer b) {
  envoy_dynamic_module_type_module_buffer buf = {b.ptr, b.length};
  return buf;
}

static void publish(envoy_dynamic_module_type_ai_filter_envoy_ptr filter, const char* key,
                    const char* value) {
  envoy_dynamic_module_callback_ai_filter_set_filter_state_bytes(filter, str_buf(key),
                                                                 str_buf(value));
}

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_ai_filter_config_module_ptr envoy_dynamic_module_on_ai_filter_config_new(
    envoy_dynamic_module_type_ai_filter_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  for (size_t i = 0; i < sizeof(scenarios) / sizeof(scenarios[0]); ++i) {
    if (name.length == strlen(scenarios[i].name) &&
        memcmp(name.ptr, scenarios[i].name, name.length) == 0) {
      ++live_configs;
      return &scenarios[i];
    }
  }
  return NULL;
}

void envoy_dynamic_module_on_ai_filter_config_destroy(
    envoy_dynamic_module_type_ai_filter_config_module_ptr config_module_ptr) {
  --live_configs;
}

envoy_dynamic_module_type_on_ai_filter_new_status
envoy_dynamic_module_on_ai_filter_new(envoy_dynamic_module_type_ai_filter_config_module_ptr config,
                                      envoy_dynamic_module_type_ai_filter_envoy_ptr filter,
                                      envoy_dynamic_module_type_ai_filter_module_ptr* filter_out) {
  const scenario value = ((const scenario_entry*)config)->value;
  envoy_dynamic_module_type_envoy_buffer header;
  if (value == scenario_skip_on_header &&
      envoy_dynamic_module_callback_ai_filter_get_request_header_value(filter, str_buf("x-skip"),
                                                                       &header, 0, NULL)) {
    return envoy_dynamic_module_type_on_ai_filter_new_status_Skip;
  }
  if (value == scenario_fail_new) {
    return envoy_dynamic_module_type_on_ai_filter_new_status_Error;
  }
  if (value == scenario_null_filter) {
    return envoy_dynamic_module_type_on_ai_filter_new_status_Created;
  }
  if (value == scenario_outside_request) {
    envoy_dynamic_module_type_envoy_buffer result;
    char observed[64];
    snprintf(observed, sizeof(observed), "%d,%d,%d,%d,%d",
             envoy_dynamic_module_callback_ai_filter_get_request_json(filter, str_buf(""), &result),
             envoy_dynamic_module_callback_ai_filter_set_request_json(filter, str_buf("/a"),
                                                                      str_buf("1")),
             envoy_dynamic_module_callback_ai_filter_remove_request_json(filter, str_buf("/a")),
             envoy_dynamic_module_callback_ai_filter_get_request_protocol(filter, &result),
             envoy_dynamic_module_callback_ai_filter_send_local_reply(filter, 403, str_buf("")));
    publish(filter, "outside_request", observed);
  }
  int* in_module_filter = malloc(sizeof(int));
  if (in_module_filter == NULL) {
    return envoy_dynamic_module_type_on_ai_filter_new_status_Error;
  }
  *in_module_filter = value;
  ++live_filters;
  *filter_out = in_module_filter;
  return envoy_dynamic_module_type_on_ai_filter_new_status_Created;
}

void envoy_dynamic_module_on_ai_filter_destroy(
    envoy_dynamic_module_type_ai_filter_module_ptr filter_module_ptr) {
  free((void*)filter_module_ptr);
  --live_filters;
}

static void rewrite(envoy_dynamic_module_type_ai_filter_envoy_ptr filter) {
  envoy_dynamic_module_type_envoy_buffer value;
  if (envoy_dynamic_module_callback_ai_filter_get_request_protocol(filter, &value)) {
    envoy_dynamic_module_callback_ai_filter_set_request_header(filter, str_buf("x-protocol"),
                                                               envoy_buf(value));
  }
  if (envoy_dynamic_module_callback_ai_filter_get_request_json(filter, str_buf("/model"), &value) ==
      envoy_dynamic_module_type_ai_filter_json_result_Ok) {
    envoy_dynamic_module_callback_ai_filter_set_request_header(filter, str_buf("x-model"),
                                                               envoy_buf(value));
  }
  envoy_dynamic_module_callback_ai_filter_set_request_json(filter, str_buf("/model"),
                                                           str_buf("\"rewritten\""));
  envoy_dynamic_module_callback_ai_filter_set_request_json(filter, str_buf("/metadata"),
                                                           str_buf("{\"tagged\":true}"));
  envoy_dynamic_module_callback_ai_filter_set_request_json(
      filter, str_buf("/messages/-"), str_buf("{\"role\":\"system\",\"content\":\"appended\"}"));
  envoy_dynamic_module_callback_ai_filter_remove_request_json(filter, str_buf("/temperature"));

  char observed[16];
  snprintf(observed, sizeof(observed), "%d",
           envoy_dynamic_module_callback_ai_filter_get_request_json(
               filter, str_buf("/messages/0/content"), &value));
  publish(filter, "first_content", observed);

  if (envoy_dynamic_module_callback_ai_filter_get_request_header_value(filter, str_buf("x-tenant"),
                                                                       &value, 0, NULL)) {
    envoy_dynamic_module_callback_ai_filter_set_filter_state_bytes(filter, str_buf("tenant"),
                                                                   envoy_buf(value));
  }
  if (envoy_dynamic_module_callback_ai_filter_get_filter_state_bytes(filter, str_buf("tenant"),
                                                                     &value)) {
    envoy_dynamic_module_callback_ai_filter_set_request_header(filter, str_buf("x-tenant-echo"),
                                                               envoy_buf(value));
  }
  envoy_dynamic_module_callback_ai_filter_remove_request_header(filter, str_buf("x-remove-me"));
}

static void write_invalid_headers(envoy_dynamic_module_type_ai_filter_envoy_ptr filter) {
  const envoy_dynamic_module_type_module_buffer nul_value = {"a\0b", 3};
  envoy_dynamic_module_type_envoy_buffer value;
  char observed[32];
  snprintf(
      observed, sizeof(observed), "%d,%d,%d,%d,%d,%d,%d",
      envoy_dynamic_module_callback_ai_filter_set_request_header(filter, str_buf(""), str_buf("v")),
      envoy_dynamic_module_callback_ai_filter_set_request_header(filter, str_buf("bad name"),
                                                                 str_buf("v")),
      envoy_dynamic_module_callback_ai_filter_set_request_header(filter, str_buf("x-crlf"),
                                                                 str_buf("a\r\nx-injected: 1")),
      envoy_dynamic_module_callback_ai_filter_set_request_header(filter, str_buf("x-nul"),
                                                                 nul_value),
      envoy_dynamic_module_callback_ai_filter_get_request_header_value(filter, str_buf("bad\nname"),
                                                                       &value, 0, NULL),
      envoy_dynamic_module_callback_ai_filter_remove_request_header(filter, str_buf("bad name")),
      envoy_dynamic_module_callback_ai_filter_set_request_header(filter, str_buf("x-valid"),
                                                                 str_buf("ok")));
  publish(filter, "invalid_headers", observed);
}

static void reply_locally(envoy_dynamic_module_type_ai_filter_envoy_ptr filter) {
  char observed[32];
  const bool out_of_range =
      envoy_dynamic_module_callback_ai_filter_send_local_reply(filter, 99, str_buf(""));
  const bool first =
      envoy_dynamic_module_callback_ai_filter_send_local_reply(filter, 403, str_buf("blocked"));
  const bool second =
      envoy_dynamic_module_callback_ai_filter_send_local_reply(filter, 500, str_buf("again"));
  snprintf(observed, sizeof(observed), "%d,%d,%d", out_of_range, first, second);
  publish(filter, "local_reply", observed);
}

envoy_dynamic_module_type_on_ai_filter_request_status
envoy_dynamic_module_on_ai_filter_request(envoy_dynamic_module_type_ai_filter_envoy_ptr filter,
                                          envoy_dynamic_module_type_ai_filter_module_ptr state) {
  switch (*(const int*)state) {
  case scenario_error:
    return envoy_dynamic_module_type_on_ai_filter_request_status_Error;
  case scenario_local_reply:
    reply_locally(filter);
    // Ignored: the local reply already ended the request.
    return envoy_dynamic_module_type_on_ai_filter_request_status_Error;
  case scenario_rewrite:
    rewrite(filter);
    break;
  case scenario_invalid_headers:
    write_invalid_headers(filter);
    break;
  default:
    break;
  }
  return envoy_dynamic_module_type_on_ai_filter_request_status_Continue;
}
