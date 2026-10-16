#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// TLS certificate selector module that exercises the dynamic certificates and the metrics. A server
// name with the "dynamic.", "decline." or "many." prefix selects the dynamic certificate of that
// name, the "www." prefix lets Envoy select the certificate, and any other server name selects the
// first certificate. The requested hook declines the names with the "decline." prefix, and counts
// its calls. The scheduled hook adds the event ids to a counter. A non-empty config is a PEM
// certificate chain followed by its private key, which the module provides as the dynamic
// certificate "dynamic.prefetch.lyft.com", and as 32 other certificates followed by the requested
// one for the names with the "many." prefix.

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

typedef struct {
  envoy_dynamic_module_type_tls_certificate_selector_config_envoy_ptr envoy_ptr;
  size_t selected_counter_id;
  size_t requested_counter_id;
  size_t scheduled_counter_id;
  char* pem;
  size_t pem_length;
} config;

// Envoy reads the selected name after the select hook returns, so it cannot point into the server
// name, which is valid only during the hook.
static _Thread_local char selected_name[256];

static bool has_prefix(envoy_dynamic_module_type_envoy_buffer buffer, const char* prefix) {
  return buffer.length >= strlen(prefix) && memcmp(buffer.ptr, prefix, strlen(prefix)) == 0;
}

static envoy_dynamic_module_type_module_buffer module_buffer(const char* value) {
  envoy_dynamic_module_type_module_buffer buffer = {value, strlen(value)};
  return buffer;
}

static bool set_certificate(const config* c, envoy_dynamic_module_type_module_buffer name) {
  envoy_dynamic_module_type_module_buffer pem = {c->pem, c->pem_length};
  return envoy_dynamic_module_callback_tls_certificate_selector_config_set_certificate(
      c->envoy_ptr, name, pem, pem, module_buffer(""));
}

envoy_dynamic_module_type_tls_certificate_selector_config_module_ptr
envoy_dynamic_module_on_tls_certificate_selector_config_new(
    envoy_dynamic_module_type_tls_certificate_selector_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name,
    envoy_dynamic_module_type_envoy_buffer config_buffer, bool is_upstream) {
  (void)name;
  (void)is_upstream;
  config* c = calloc(1, sizeof(config));
  c->envoy_ptr = config_envoy_ptr;
  envoy_dynamic_module_type_module_buffer label_name = module_buffer("outcome");
  if (envoy_dynamic_module_callback_tls_certificate_selector_config_define_counter(
          config_envoy_ptr, module_buffer("selected"), &label_name, 1, &c->selected_counter_id) !=
          envoy_dynamic_module_type_metrics_result_Success ||
      envoy_dynamic_module_callback_tls_certificate_selector_config_define_counter(
          config_envoy_ptr, module_buffer("requested"), NULL, 0, &c->requested_counter_id) !=
          envoy_dynamic_module_type_metrics_result_Success ||
      envoy_dynamic_module_callback_tls_certificate_selector_config_define_counter(
          config_envoy_ptr, module_buffer("scheduled"), NULL, 0, &c->scheduled_counter_id) !=
          envoy_dynamic_module_type_metrics_result_Success) {
    free(c);
    return NULL;
  }
  if (config_buffer.length > 0) {
    c->pem = malloc(config_buffer.length);
    memcpy(c->pem, config_buffer.ptr, config_buffer.length);
    c->pem_length = config_buffer.length;
    if (!set_certificate(c, module_buffer("dynamic.prefetch.lyft.com"))) {
      free(c->pem);
      free(c);
      return NULL;
    }
  }
  return c;
}

void envoy_dynamic_module_on_tls_certificate_selector_config_destroy(
    envoy_dynamic_module_type_tls_certificate_selector_config_module_ptr config_module_ptr) {
  config* c = (config*)config_module_ptr;
  free(c->pem);
  free(c);
}

envoy_dynamic_module_type_tls_certificate_selector_module_ptr
envoy_dynamic_module_on_tls_certificate_selector_new(
    envoy_dynamic_module_type_tls_certificate_selector_config_module_ptr config_module_ptr,
    const envoy_dynamic_module_type_envoy_buffer* certificates, size_t certificates_count) {
  (void)certificates;
  (void)certificates_count;
  return config_module_ptr;
}

void envoy_dynamic_module_on_tls_certificate_selector_destroy(
    envoy_dynamic_module_type_tls_certificate_selector_module_ptr selector_module_ptr) {
  (void)selector_module_ptr;
}

envoy_dynamic_module_type_tls_certificate_selector_status
envoy_dynamic_module_on_tls_certificate_selector_select(
    envoy_dynamic_module_type_tls_certificate_selector_module_ptr selector_module_ptr,
    envoy_dynamic_module_type_tls_certificate_selector_context_envoy_ptr context_envoy_ptr,
    size_t* certificate_index, envoy_dynamic_module_type_module_buffer* certificate_name) {
  const config* c = (const config*)selector_module_ptr;
  envoy_dynamic_module_type_envoy_buffer server_name = {NULL, 0};
  envoy_dynamic_module_callback_tls_certificate_selector_get_requested_server_name(
      context_envoy_ptr, &server_name);
  envoy_dynamic_module_type_tls_certificate_selector_status status;
  envoy_dynamic_module_type_module_buffer outcome;
  if ((has_prefix(server_name, "dynamic.") || has_prefix(server_name, "decline.") ||
       has_prefix(server_name, "many.")) &&
      server_name.length <= sizeof(selected_name)) {
    memcpy(selected_name, server_name.ptr, server_name.length);
    certificate_name->ptr = selected_name;
    certificate_name->length = server_name.length;
    status = envoy_dynamic_module_type_tls_certificate_selector_status_Dynamic;
    outcome = module_buffer("dynamic");
  } else if (has_prefix(server_name, "www.")) {
    status = envoy_dynamic_module_type_tls_certificate_selector_status_Default;
    outcome = module_buffer("default");
  } else {
    *certificate_index = 0;
    status = envoy_dynamic_module_type_tls_certificate_selector_status_Configured;
    outcome = module_buffer("configured");
  }
  envoy_dynamic_module_callback_tls_certificate_selector_config_increment_counter(
      c->envoy_ptr, c->selected_counter_id, &outcome, 1, 1);
  return status;
}

void envoy_dynamic_module_on_tls_certificate_selector_config_certificate_requested(
    envoy_dynamic_module_type_tls_certificate_selector_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_tls_certificate_selector_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_envoy_buffer name) {
  const config* c = (const config*)config_module_ptr;
  envoy_dynamic_module_callback_tls_certificate_selector_config_increment_counter(
      config_envoy_ptr, c->requested_counter_id, NULL, 0, 1);
  envoy_dynamic_module_type_module_buffer certificate_name = {name.ptr, name.length};
  if (has_prefix(name, "decline.")) {
    envoy_dynamic_module_callback_tls_certificate_selector_config_remove_certificate(
        config_envoy_ptr, certificate_name);
  } else if (has_prefix(name, "many.") && c->pem_length > 0) {
    char other_name[32];
    for (int i = 0; i < 32; i++) {
      snprintf(other_name, sizeof(other_name), "dynamic.%d.lyft.com", i);
      set_certificate(c, module_buffer(other_name));
    }
    set_certificate(c, certificate_name);
  }
}

void envoy_dynamic_module_on_tls_certificate_selector_config_scheduled(
    envoy_dynamic_module_type_tls_certificate_selector_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_tls_certificate_selector_config_module_ptr config_module_ptr,
    uint64_t event_id) {
  const config* c = (const config*)config_module_ptr;
  envoy_dynamic_module_callback_tls_certificate_selector_config_increment_counter(
      config_envoy_ptr, c->scheduled_counter_id, NULL, 0, event_id);
}
