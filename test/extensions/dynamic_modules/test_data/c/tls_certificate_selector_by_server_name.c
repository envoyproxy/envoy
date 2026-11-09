#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// TLS certificate selector module that selects the first certificate whose DER encoding contains
// the server name, and the last certificate when there is no server name. It fails the selection
// when no certificate matches. A config of "downstream" or "upstream" rejects the other direction.

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

typedef struct {
  size_t count;
  envoy_dynamic_module_type_envoy_buffer* certificates;
} selector;

static int config_dummy = 0;

static bool equals(envoy_dynamic_module_type_envoy_buffer buffer, const char* value) {
  return buffer.length == strlen(value) && memcmp(buffer.ptr, value, buffer.length) == 0;
}

static bool contains(envoy_dynamic_module_type_envoy_buffer haystack,
                     envoy_dynamic_module_type_envoy_buffer needle) {
  for (size_t i = 0; i + needle.length <= haystack.length; i++) {
    if (memcmp(haystack.ptr + i, needle.ptr, needle.length) == 0) {
      return true;
    }
  }
  return false;
}

envoy_dynamic_module_type_tls_certificate_selector_config_module_ptr
envoy_dynamic_module_on_tls_certificate_selector_config_new(
    envoy_dynamic_module_type_tls_certificate_selector_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config,
    bool is_upstream) {
  (void)config_envoy_ptr;
  (void)name;
  if ((is_upstream && equals(config, "downstream")) ||
      (!is_upstream && equals(config, "upstream"))) {
    return NULL;
  }
  return &config_dummy;
}

void envoy_dynamic_module_on_tls_certificate_selector_config_destroy(
    envoy_dynamic_module_type_tls_certificate_selector_config_module_ptr config_module_ptr) {
  // Envoy only destroys the configurations that the module created.
  if (config_module_ptr != &config_dummy) {
    abort();
  }
}

envoy_dynamic_module_type_tls_certificate_selector_module_ptr
envoy_dynamic_module_on_tls_certificate_selector_new(
    envoy_dynamic_module_type_tls_certificate_selector_config_module_ptr config_module_ptr,
    const envoy_dynamic_module_type_envoy_buffer* certificates, size_t certificates_count) {
  (void)config_module_ptr;
  // The certificates are valid only during this call, so keep a copy of each.
  selector* s = malloc(sizeof(selector));
  s->count = certificates_count;
  s->certificates = calloc(certificates_count, sizeof(envoy_dynamic_module_type_envoy_buffer));
  for (size_t i = 0; i < certificates_count; i++) {
    char* copy = malloc(certificates[i].length + 1);
    if (certificates[i].length > 0) {
      memcpy(copy, certificates[i].ptr, certificates[i].length);
    }
    s->certificates[i].ptr = copy;
    s->certificates[i].length = certificates[i].length;
  }
  return s;
}

void envoy_dynamic_module_on_tls_certificate_selector_destroy(
    envoy_dynamic_module_type_tls_certificate_selector_module_ptr selector_module_ptr) {
  selector* s = (selector*)selector_module_ptr;
  for (size_t i = 0; i < s->count; i++) {
    free((void*)s->certificates[i].ptr);
  }
  free(s->certificates);
  free(s);
}

envoy_dynamic_module_type_tls_certificate_selector_status
envoy_dynamic_module_on_tls_certificate_selector_select(
    envoy_dynamic_module_type_tls_certificate_selector_module_ptr selector_module_ptr,
    envoy_dynamic_module_type_tls_certificate_selector_context_envoy_ptr context_envoy_ptr,
    size_t* certificate_index, envoy_dynamic_module_type_module_buffer* certificate_name) {
  (void)certificate_name;
  const selector* s = (const selector*)selector_module_ptr;
  envoy_dynamic_module_type_envoy_buffer server_name = {NULL, 0};
  if (!envoy_dynamic_module_callback_tls_certificate_selector_get_requested_server_name(
          context_envoy_ptr, &server_name)) {
    // Envoy passes an entry even when the TLS context has no certificate.
    *certificate_index = s->count - 1;
    return envoy_dynamic_module_type_tls_certificate_selector_status_Configured;
  }
  for (size_t i = 0; i < s->count; i++) {
    if (contains(s->certificates[i], server_name)) {
      *certificate_index = i;
      return envoy_dynamic_module_type_tls_certificate_selector_status_Configured;
    }
  }
  return envoy_dynamic_module_type_tls_certificate_selector_status_Failed;
}
