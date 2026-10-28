#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// This module builds its routes through the built route cache so that a test can observe what the
// cache does. The specifier config selects the mode, and the key is read from the x-key request
// header, defaulting to 1. On a hit the module records nothing, as a module that trusts the cache
// does.

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

enum mode {
  // Selects the template "only" and records a cluster, metadata and user data on a miss.
  CACHED,
  // Records nothing on a miss and only a cluster on a verified hit, so the template is cached as it
  // is and the verified hit finds a difference.
  INCONSISTENT,
  // Like INCONSISTENT, but records only route metadata on a verified hit.
  INCONSISTENT_METADATA,
  // Selects the direct response template "direct", recording nothing on a miss and a cluster on a
  // verified hit, which fails the decision.
  FAILING_VERIFY,
  // Selects the template "only" and records nothing, so the template itself is cached.
  BARE,
  // Records only route metadata, which needs no route entry wrapper.
  ROUTE_ONLY,
  // Selects the route override "with_metadata", whose metadata is layered into the recording.
  OVERRIDE_METADATA,
  // Records the cluster and the field the x-vary header names, set from the x-value header.
  VARY,
  // Selects the template the x-template header names and records nothing.
  TEMPLATE_BY_HEADER,
  // Selects the direct response template "direct" and records a cluster, which fails the decision.
  DIRECT_ENTRY,
  // Selects the template "weighted", whose cluster is picked per request.
  WEIGHTED,
  // Records the route without setting a key.
  NO_KEY,
  // Sets a key without selecting a template.
  KEY_WITHOUT_TEMPLATE,
  // Selects the template again after the lookup, which discards it.
  RESELECT,
  // Unsets the template after the lookup, which discards it, and selects the route override
  // "regex" for the route matching resolved.
  UNSET_AFTER_KEY,
  // Sets a second key after the first, which replaces the lookup.
  REKEY,
  // Records a rule and then replaces it with another, which the cache does not check.
  REPLACED_RULE,
  // Records a path with set_prefix_rewrite, which depends on the request path.
  PREFIX_REWRITE,
  // Records a path with set_prefix_rewrite and then replaces it with set_path.
  PREFIX_REWRITE_THEN_PATH,
  // Records the path /internal/v1 with set_path on a miss, and the same path for /api/v1 with
  // set_prefix_rewrite on a verified hit, which depends on the request path.
  PREFIX_REWRITE_WHEN_VERIFIED,
  // Records a prefix rewrite rule of /api and rule_cluster, or fallback_cluster when the rule is
  // rejected for a path it does not apply to.
  RULE,
  // Records the rule and rule_cluster for a path under /api, and only fallback_cluster otherwise.
  RULE_FOR_API,
  // Selects the route override "regex", whose regex rewrite the cached route applies per request.
  OVERRIDE_REGEX,
  // Records another cluster and user data on a hit, which Envoy ignores.
  HIT_THEN_RECORD,
  // Record a decision, or return a status, that wins over a hit.
  HIT_THEN_NO_ROUTE,
  HIT_THEN_PASS_THROUGH,
  HIT_THEN_ERROR,
  HIT_THEN_SKIP,
  MODE_COUNT,
};

static const char* const mode_names[MODE_COUNT] = {
    "cached",
    "inconsistent",
    "inconsistent-metadata",
    "failing-verify",
    "bare",
    "route-only",
    "override-metadata",
    "vary",
    "template-by-header",
    "direct-entry",
    "weighted",
    "no-key",
    "key-without-template",
    "reselect",
    "unset-after-key",
    "rekey",
    "replaced-rule",
    "prefix-rewrite",
    "prefix-rewrite-then-path",
    "prefix-rewrite-when-verified",
    "rule",
    "rule-for-api",
    "override-regex",
    "hit-then-record",
    "hit-then-no-route",
    "hit-then-pass-through",
    "hit-then-error",
    "hit-then-skip",
};

// Distinct addresses used as the config handle that selects the mode.
static char modes[MODE_COUNT];

static int last_cache_result = -1;
static int route_destroy_count = 0;
static int config_destroy_count = 0;

// Let tests observe what the cache returned and which hooks Envoy invoked.
int getLastCacheResult(void) { return last_cache_result; }
int getRouteDestroyCount(void) { return route_destroy_count; }
int getConfigDestroyCount(void) { return config_destroy_count; }

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
    envoy_dynamic_module_type_route_specifier_config_module_ptr config_module_ptr) {
  config_destroy_count++;
}

void envoy_dynamic_module_on_route_specifier_route_destroy(
    envoy_dynamic_module_type_route_specifier_config_module_ptr config_module_ptr,
    uint64_t user_data) {
  route_destroy_count++;
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

static uint64_t number(envoy_dynamic_module_type_envoy_buffer value, uint64_t missing) {
  if (value.ptr == NULL) {
    return missing;
  }
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

// Writes the prefix followed by the value into out, so that two values name two different things.
static envoy_dynamic_module_type_module_buffer named(char* out, size_t size, const char* prefix,
                                                     envoy_dynamic_module_type_envoy_buffer value) {
  const int written =
      snprintf(out, size, "%s%.*s", prefix, (int)value.length, value.ptr != NULL ? value.ptr : "");
  size_t length = written > 0 ? (size_t)written : 0;
  if (length >= size) {
    length = size - 1;
  }
  envoy_dynamic_module_type_module_buffer result = {out, length};
  return result;
}

static void record_metadata(envoy_dynamic_module_type_route_specifier_context_envoy_ptr ctx) {
  envoy_dynamic_module_callback_route_specifier_set_route_metadata_string(
      ctx, buffer("envoy.test.cache"), buffer("built"), buffer("true"));
}

static void record_route(envoy_dynamic_module_type_route_specifier_context_envoy_ptr ctx,
                         const char* cluster) {
  envoy_dynamic_module_callback_route_specifier_set_cluster_name(ctx, buffer(cluster));
  record_metadata(ctx);
  envoy_dynamic_module_callback_route_specifier_set_route_user_data(ctx, 7);
}

// Records cached_cluster and the one field x-vary names, so that two requests that carry different
// values of x-value record different routes for one key.
static void record_varied(envoy_dynamic_module_type_route_specifier_context_envoy_ptr ctx) {
  envoy_dynamic_module_callback_route_specifier_set_cluster_name(ctx, buffer("cached_cluster"));
  const envoy_dynamic_module_type_envoy_buffer field = header(ctx, "x-vary");
  const envoy_dynamic_module_type_envoy_buffer value = header(ctx, "x-value");
  const envoy_dynamic_module_type_module_buffer value_buffer = {value.ptr, value.length};
  const uint64_t value_number = number(value, 0);
  const envoy_dynamic_module_type_route_specifier_header_append_action overwrite =
      envoy_dynamic_module_type_route_specifier_header_append_action_OverwriteIfExistsOrAdd;
  char name[64];
  if (header_is(field, "cluster")) {
    envoy_dynamic_module_callback_route_specifier_set_cluster_name(
        ctx, named(name, sizeof(name), "cluster_", value));
  } else if (header_is(field, "timeout")) {
    envoy_dynamic_module_callback_route_specifier_set_timeout(ctx, value_number);
  } else if (header_is(field, "idle-timeout")) {
    envoy_dynamic_module_callback_route_specifier_set_idle_timeout(ctx, value_number);
  } else if (header_is(field, "max-stream-duration")) {
    envoy_dynamic_module_callback_route_specifier_set_max_stream_duration(ctx, value_number);
  } else if (header_is(field, "body-buffer-limit")) {
    envoy_dynamic_module_callback_route_specifier_set_request_body_buffer_limit(ctx, value_number);
  } else if (header_is(field, "priority")) {
    envoy_dynamic_module_callback_route_specifier_set_priority(
        ctx, value_number == 1 ? envoy_dynamic_module_type_resource_priority_Default
                               : envoy_dynamic_module_type_resource_priority_High);
  } else if (header_is(field, "cluster-not-found")) {
    envoy_dynamic_module_callback_route_specifier_set_cluster_not_found_response_code(
        ctx, value_number == 1 ? 503 : 404);
  } else if (header_is(field, "override")) {
    envoy_dynamic_module_callback_route_specifier_set_route_override(
        ctx, named(name, sizeof(name), "override_", value));
  } else if (header_is(field, "metadata")) {
    envoy_dynamic_module_callback_route_specifier_set_route_metadata_string(
        ctx, buffer("envoy.test.cache"), buffer("vary"), value_buffer);
  } else if (header_is(field, "filter-disabled")) {
    envoy_dynamic_module_callback_route_specifier_set_filter_disabled(
        ctx, named(name, sizeof(name), "filter_", value), true);
  } else if (header_is(field, "route-name")) {
    envoy_dynamic_module_callback_route_specifier_set_route_name(ctx, value_buffer);
  } else if (header_is(field, "path")) {
    envoy_dynamic_module_callback_route_specifier_set_path(ctx,
                                                           named(name, sizeof(name), "/", value));
  } else if (header_is(field, "rule")) {
    envoy_dynamic_module_callback_route_specifier_set_prefix_rewrite_rule(
        ctx, envoy_dynamic_module_type_route_specifier_rewrite_span_Literal, buffer("/api"),
        named(name, sizeof(name), "/", value));
  } else if (header_is(field, "host")) {
    envoy_dynamic_module_callback_route_specifier_set_host(
        ctx, named(name, sizeof(name), "host", value));
  } else if (header_is(field, "request-header-add")) {
    envoy_dynamic_module_callback_route_specifier_add_request_header(ctx, buffer("x-varied"),
                                                                     value_buffer, overwrite);
  } else if (header_is(field, "request-header-remove")) {
    envoy_dynamic_module_callback_route_specifier_remove_request_header(
        ctx, named(name, sizeof(name), "x-removed-", value));
  } else if (header_is(field, "response-header-add")) {
    envoy_dynamic_module_callback_route_specifier_add_response_header(ctx, buffer("x-varied"),
                                                                      value_buffer, overwrite);
  } else if (header_is(field, "response-header-remove")) {
    envoy_dynamic_module_callback_route_specifier_remove_response_header(
        ctx, named(name, sizeof(name), "x-removed-", value));
  } else if (header_is(field, "user-data")) {
    envoy_dynamic_module_callback_route_specifier_set_route_user_data(ctx, value_number);
  }
}

static int record_rule(envoy_dynamic_module_type_route_specifier_context_envoy_ptr ctx) {
  return envoy_dynamic_module_callback_route_specifier_set_prefix_rewrite_rule(
      ctx, envoy_dynamic_module_type_route_specifier_rewrite_span_Literal, buffer("/api"),
      buffer("/internal"));
}

static void select_template(envoy_dynamic_module_type_route_specifier_context_envoy_ptr ctx,
                            enum mode mode) {
  if (mode == TEMPLATE_BY_HEADER) {
    const envoy_dynamic_module_type_envoy_buffer selected = header(ctx, "x-template");
    const envoy_dynamic_module_type_module_buffer selected_buffer = {selected.ptr, selected.length};
    envoy_dynamic_module_callback_route_specifier_set_route_template(ctx, selected_buffer);
    return;
  }
  const char* template_id = "only";
  if (mode == DIRECT_ENTRY || mode == FAILING_VERIFY) {
    template_id = "direct";
  } else if (mode == WEIGHTED) {
    template_id = "weighted";
  }
  envoy_dynamic_module_callback_route_specifier_set_route_template(ctx, buffer(template_id));
}

envoy_dynamic_module_type_route_specifier_on_route_status
envoy_dynamic_module_on_route_specifier_on_route(
    envoy_dynamic_module_type_route_specifier_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_route_specifier_context_envoy_ptr ctx) {
  const enum mode mode = (enum mode)((const char*)config_module_ptr - modes);
  const envoy_dynamic_module_type_route_specifier_on_route_status stop =
      envoy_dynamic_module_type_route_specifier_on_route_status_StopIteration;
  const uint64_t key = number(header(ctx, "x-key"), 1);
  last_cache_result = -1;
  if (mode == KEY_WITHOUT_TEMPLATE) {
    last_cache_result = envoy_dynamic_module_callback_route_specifier_lookup_built_route(ctx, key);
    envoy_dynamic_module_callback_route_specifier_set_cluster_name(ctx, buffer("uncached"));
    return stop;
  }
  select_template(ctx, mode);
  if (mode == NO_KEY) {
    record_route(ctx, "cached_cluster");
    return stop;
  }
  last_cache_result = envoy_dynamic_module_callback_route_specifier_lookup_built_route(ctx, key);
  if (mode == RESELECT) {
    select_template(ctx, mode);
  } else if (mode == UNSET_AFTER_KEY) {
    envoy_dynamic_module_callback_route_specifier_unset_route_template(ctx, buffer("only"));
    envoy_dynamic_module_callback_route_specifier_set_route_override(ctx, buffer("regex"));
    return stop;
  } else if (mode == REKEY) {
    last_cache_result =
        envoy_dynamic_module_callback_route_specifier_lookup_built_route(ctx, key + 100);
  }
  if (last_cache_result ==
      envoy_dynamic_module_type_route_specifier_built_route_lookup_result_Hit) {
    switch (mode) {
    case HIT_THEN_RECORD:
      envoy_dynamic_module_callback_route_specifier_set_cluster_name(ctx,
                                                                     buffer("ignored_cluster"));
      envoy_dynamic_module_callback_route_specifier_set_route_user_data(ctx, 9);
      break;
    case HIT_THEN_NO_ROUTE:
      envoy_dynamic_module_callback_route_specifier_set_decision(
          ctx, envoy_dynamic_module_type_route_specifier_decision_NoRoute);
      break;
    case HIT_THEN_PASS_THROUGH:
      envoy_dynamic_module_callback_route_specifier_set_decision(
          ctx, envoy_dynamic_module_type_route_specifier_decision_PassThrough);
      break;
    case HIT_THEN_ERROR:
      envoy_dynamic_module_callback_route_specifier_set_decision(
          ctx, envoy_dynamic_module_type_route_specifier_decision_Error);
      break;
    case HIT_THEN_SKIP:
      return envoy_dynamic_module_type_route_specifier_on_route_status_StopIterationAndSkipRoute;
    default:
      break;
    }
    return stop;
  }
  const int verified =
      last_cache_result ==
      envoy_dynamic_module_type_route_specifier_built_route_lookup_result_HitVerify;
  switch (mode) {
  case INCONSISTENT:
  case FAILING_VERIFY:
    if (verified) {
      envoy_dynamic_module_callback_route_specifier_set_cluster_name(ctx, buffer("cluster_b"));
    }
    break;
  case INCONSISTENT_METADATA:
    if (verified) {
      record_metadata(ctx);
    }
    break;
  case BARE:
  case TEMPLATE_BY_HEADER:
    break;
  case ROUTE_ONLY:
  case WEIGHTED:
    record_metadata(ctx);
    break;
  case OVERRIDE_METADATA:
    envoy_dynamic_module_callback_route_specifier_set_route_override(ctx, buffer("with_metadata"));
    break;
  case VARY:
    record_varied(ctx);
    break;
  case PREFIX_REWRITE:
    record_route(ctx, "cached_cluster");
    envoy_dynamic_module_callback_route_specifier_set_prefix_rewrite(ctx, buffer("/api"),
                                                                     buffer("/rewritten"));
    break;
  case PREFIX_REWRITE_THEN_PATH:
    record_route(ctx, "cached_cluster");
    envoy_dynamic_module_callback_route_specifier_set_prefix_rewrite(ctx, buffer("/api"),
                                                                     buffer("/rewritten"));
    envoy_dynamic_module_callback_route_specifier_set_path(ctx, buffer("/literal"));
    break;
  case PREFIX_REWRITE_WHEN_VERIFIED:
    if (verified) {
      envoy_dynamic_module_callback_route_specifier_set_prefix_rewrite(ctx, buffer("/api"),
                                                                       buffer("/internal"));
    } else {
      envoy_dynamic_module_callback_route_specifier_set_path(ctx, buffer("/internal/v1"));
    }
    break;
  case RULE:
    envoy_dynamic_module_callback_route_specifier_set_cluster_name(
        ctx, buffer(record_rule(ctx) ? "rule_cluster" : "fallback_cluster"));
    break;
  case REPLACED_RULE:
    record_rule(ctx);
    envoy_dynamic_module_callback_route_specifier_set_prefix_rewrite_rule(
        ctx, envoy_dynamic_module_type_route_specifier_rewrite_span_WholePath, buffer(""),
        buffer("/fixed"));
    break;
  case RULE_FOR_API: {
    const envoy_dynamic_module_type_envoy_buffer path = header(ctx, ":path");
    if (path.ptr != NULL && path.length >= 4 && memcmp(path.ptr, "/api", 4) == 0) {
      record_rule(ctx);
      envoy_dynamic_module_callback_route_specifier_set_cluster_name(ctx, buffer("rule_cluster"));
    } else {
      envoy_dynamic_module_callback_route_specifier_set_cluster_name(ctx,
                                                                     buffer("fallback_cluster"));
    }
    break;
  }
  case OVERRIDE_REGEX:
    envoy_dynamic_module_callback_route_specifier_set_route_override(ctx, buffer("regex"));
    break;
  default:
    record_route(ctx, "cached_cluster");
    break;
  }
  return stop;
}
