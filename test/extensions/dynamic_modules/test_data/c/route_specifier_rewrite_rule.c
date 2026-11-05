#include <string.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// This module records prefix rewrite rules so that a test can finalize the produced route for
// requests it was not built for. The specifier config selects the mode, and every mode records
// whether its last call was accepted as the route metadata envoy.test.rule.accepted.

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

enum mode {
  LITERAL,
  WHOLE_PATH,
  NOT_PREFIX,
  WHOLE_PATH_WITH_MATCHED,
  EMPTY_REPLACEMENT,
  INVALID_REPLACEMENT,
  UNKNOWN_SPAN,
  TOO_LONG,
  PATH_WINS,
  RULE_OVER_OVERRIDE,
  OVERRIDE_ONLY,
  REPLACED,
  REJECTED_AFTER_ACCEPTED,
  NO_REWRITE,
  RELATIVE_REPLACEMENT,
  QUERY_IN_REPLACEMENT,
  FRAGMENT_IN_REPLACEMENT,
  RESULT_TOO_LONG,
  PATH_OVER_OVERRIDE,
  RULE_THEN_PATH,
  RULE_THEN_OVERRIDE,
  QUERY_IN_MATCHED,
  FRAGMENT_IN_MATCHED,
  MODE_COUNT,
};

static const char* const mode_names[MODE_COUNT] = {
    "literal",
    "whole-path",
    "not-prefix",
    "whole-path-with-matched",
    "empty-replacement",
    "invalid-replacement",
    "unknown-span",
    "too-long",
    "path-wins",
    "rule-over-override",
    "override-only",
    "replaced",
    "rejected-after-accepted",
    "no-rewrite",
    "relative-replacement",
    "query-in-replacement",
    "fragment-in-replacement",
    "result-too-long",
    "path-over-override",
    "rule-then-path",
    "rule-then-override",
    "query-in-matched",
    "fragment-in-matched",
};

// Distinct addresses used as the config handle that selects the mode.
static char modes[MODE_COUNT];

envoy_dynamic_module_type_route_specifier_config_module_ptr
envoy_dynamic_module_on_route_specifier_config_new(
    envoy_dynamic_module_type_route_specifier_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  for (int i = 0; i < MODE_COUNT; i++) {
    const size_t length = strlen(mode_names[i]);
    if (config.length == length && memcmp(config.ptr, mode_names[i], length) == 0) {
      return &modes[i];
    }
  }
  return NULL;
}

void envoy_dynamic_module_on_route_specifier_config_destroy(
    envoy_dynamic_module_type_route_specifier_config_module_ptr config_module_ptr) {}

static envoy_dynamic_module_type_module_buffer buffer(const char* value) {
  envoy_dynamic_module_type_module_buffer result = {value, strlen(value)};
  return result;
}

static bool rule(envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr,
                 envoy_dynamic_module_type_route_specifier_rewrite_span span, const char* matched,
                 const char* replacement) {
  return envoy_dynamic_module_callback_route_specifier_set_prefix_rewrite_rule(
      context_envoy_ptr, span, buffer(matched), buffer(replacement));
}

envoy_dynamic_module_type_route_specifier_on_route_status
envoy_dynamic_module_on_route_specifier_on_route(
    envoy_dynamic_module_type_route_specifier_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr context_envoy_ptr) {
  const envoy_dynamic_module_type_route_specifier_rewrite_span literal =
      envoy_dynamic_module_type_route_specifier_rewrite_span_Literal;
  const envoy_dynamic_module_type_route_specifier_rewrite_span whole_path =
      envoy_dynamic_module_type_route_specifier_rewrite_span_WholePath;
  bool accepted = false;
  switch ((enum mode)((const char*)config_module_ptr - modes)) {
  case LITERAL:
    accepted = rule(context_envoy_ptr, literal, "/api", "/internal");
    break;
  case WHOLE_PATH:
    accepted = rule(context_envoy_ptr, whole_path, "", "/fixed");
    break;
  case NOT_PREFIX:
    accepted = rule(context_envoy_ptr, literal, "/nope", "/internal");
    break;
  case WHOLE_PATH_WITH_MATCHED:
    accepted = rule(context_envoy_ptr, whole_path, "/api", "/fixed");
    break;
  case EMPTY_REPLACEMENT:
    accepted = rule(context_envoy_ptr, literal, "/api", "");
    break;
  case INVALID_REPLACEMENT:
    accepted = rule(context_envoy_ptr, literal, "/api", "/in\nternal");
    break;
  case UNKNOWN_SPAN:
    // The Sentinel enumerator makes a span this module does not name well defined to pass.
    accepted = rule(context_envoy_ptr, (envoy_dynamic_module_type_route_specifier_rewrite_span)7,
                    "/api", "/internal");
    break;
  case TOO_LONG:
    accepted = rule(context_envoy_ptr, literal, "/api", "/a-replacement-longer-than-the-bound");
    break;
  case PATH_WINS:
    envoy_dynamic_module_callback_route_specifier_set_path(context_envoy_ptr, buffer("/literal"));
    accepted = rule(context_envoy_ptr, literal, "/api", "/internal");
    break;
  case RULE_OVER_OVERRIDE:
    envoy_dynamic_module_callback_route_specifier_set_route_override(context_envoy_ptr,
                                                                     buffer("rewrite"));
    accepted = rule(context_envoy_ptr, literal, "/api", "/internal");
    break;
  case OVERRIDE_ONLY:
    accepted = envoy_dynamic_module_callback_route_specifier_set_route_override(context_envoy_ptr,
                                                                                buffer("rewrite"));
    break;
  case REPLACED:
    rule(context_envoy_ptr, literal, "/api", "/first");
    accepted = rule(context_envoy_ptr, literal, "/api", "/second");
    break;
  case REJECTED_AFTER_ACCEPTED:
    rule(context_envoy_ptr, literal, "/api", "/first");
    accepted = rule(context_envoy_ptr, literal, "/nope", "/second");
    break;
  case NO_REWRITE:
    accepted = envoy_dynamic_module_callback_route_specifier_set_route_override(context_envoy_ptr,
                                                                                buffer("plain"));
    break;
  case RELATIVE_REPLACEMENT:
    accepted = rule(context_envoy_ptr, literal, "/api", "internal");
    break;
  case QUERY_IN_REPLACEMENT:
    accepted = rule(context_envoy_ptr, literal, "/api", "/internal?x=1");
    break;
  case FRAGMENT_IN_REPLACEMENT:
    accepted = rule(context_envoy_ptr, literal, "/api", "/internal#x");
    break;
  case QUERY_IN_MATCHED:
    accepted = rule(context_envoy_ptr, literal, "/api?", "/internal");
    break;
  case FRAGMENT_IN_MATCHED:
    accepted = rule(context_envoy_ptr, literal, "/api#", "/internal");
    break;
  case RESULT_TOO_LONG:
    // The replacement fits the bound, but not the path it produces for the current request.
    accepted = rule(context_envoy_ptr, literal, "/api", "/0123456789abcd");
    break;
  case PATH_OVER_OVERRIDE:
    envoy_dynamic_module_callback_route_specifier_set_path(context_envoy_ptr, buffer("/literal"));
    accepted = envoy_dynamic_module_callback_route_specifier_set_route_override(context_envoy_ptr,
                                                                                buffer("rewrite"));
    break;
  case RULE_THEN_PATH:
    accepted = rule(context_envoy_ptr, literal, "/api", "/internal");
    envoy_dynamic_module_callback_route_specifier_set_path(context_envoy_ptr, buffer("/literal"));
    break;
  case RULE_THEN_OVERRIDE:
    accepted = rule(context_envoy_ptr, literal, "/api", "/internal");
    envoy_dynamic_module_callback_route_specifier_set_route_override(context_envoy_ptr,
                                                                     buffer("rewrite"));
    break;
  case MODE_COUNT:
    break;
  }
  envoy_dynamic_module_callback_route_specifier_set_route_metadata_string(
      context_envoy_ptr, buffer("envoy.test.rule"), buffer("accepted"),
      buffer(accepted ? "true" : "false"));
  return envoy_dynamic_module_type_route_specifier_on_route_status_Continue;
}
