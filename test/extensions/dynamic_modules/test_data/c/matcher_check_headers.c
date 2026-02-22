#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// This module matches based on the presence and value of a specific request header.
// The header name is provided via matcher_config during configuration.
// At match time, the module reads request headers and checks if the configured header
// exists with the value "match".
envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

// Store the header name from configuration.
typedef struct {
  char* header_name;
  size_t header_name_length;
} matcher_config_t;

envoy_dynamic_module_type_matcher_config_module_ptr envoy_dynamic_module_on_matcher_config_new(
    envoy_dynamic_module_type_matcher_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer matcher_name,
    envoy_dynamic_module_type_envoy_buffer matcher_config) {
  // The config buffer contains the header name to look for.
  matcher_config_t* config = (matcher_config_t*)malloc(sizeof(matcher_config_t));
  config->header_name = (char*)malloc(matcher_config.length);
  memcpy(config->header_name, matcher_config.ptr, matcher_config.length);
  config->header_name_length = matcher_config.length;
  return config;
}

void envoy_dynamic_module_on_matcher_config_destroy(
    envoy_dynamic_module_type_matcher_config_module_ptr config_module_ptr) {
  matcher_config_t* config = (matcher_config_t*)config_module_ptr;
  if (config != NULL) {
    free(config->header_name);
    free(config);
  }
}

bool envoy_dynamic_module_on_matcher_match(
    envoy_dynamic_module_type_matcher_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_matcher_input_envoy_ptr matcher_input_envoy_ptr) {
  matcher_config_t* config = (matcher_config_t*)config_module_ptr;
  if (config == NULL) {
    return false;
  }

  // Get the number of request headers.
  size_t num_headers = envoy_dynamic_module_callback_matcher_get_headers_size(
      matcher_input_envoy_ptr, envoy_dynamic_module_type_http_header_type_RequestHeader);

  if (num_headers == 0) {
    return false;
  }

  // Allocate space for headers.
  envoy_dynamic_module_type_envoy_http_header* headers =
      (envoy_dynamic_module_type_envoy_http_header*)calloc(
          num_headers, sizeof(envoy_dynamic_module_type_envoy_http_header));

  bool result = envoy_dynamic_module_callback_matcher_get_headers(
      matcher_input_envoy_ptr, envoy_dynamic_module_type_http_header_type_RequestHeader, headers);

  if (!result) {
    free(headers);
    return false;
  }

  // Search for the configured header.
  bool matched = false;
  for (size_t i = 0; i < num_headers; i++) {
    if (headers[i].key_length == config->header_name_length &&
        memcmp(headers[i].key_ptr, config->header_name, config->header_name_length) == 0) {
      // Check if the value is "match".
      if (headers[i].value_length == 5 && memcmp(headers[i].value_ptr, "match", 5) == 0) {
        matched = true;
      }
      break;
    }
  }

  free(headers);
  return matched;
}

