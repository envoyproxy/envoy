#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// A header formatter module that restores the casing the peer used. It remembers every key passed
// to process_key and, when format is asked for the lower-cased form of a remembered key, returns
// the remembered spelling. Keys it never saw are upper-cased instead, which makes it observable in
// tests that format is also called for headers Envoy adds itself.
//
// The remembered keys live in the per-message formatter, so this also exercises the fact that each
// message gets its own instance: keys seen on one message must not leak into another.

#define MAX_KEYS 32
#define MAX_KEY_LEN 64

typedef struct {
  char keys[MAX_KEYS][MAX_KEY_LEN];
  size_t key_count;
  // Holds the value returned by the last format call. The ABI requires it to stay valid until the
  // next call into the module, so it cannot be a local.
  char formatted[MAX_KEY_LEN];
} header_formatter;

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_header_formatter_config_module_ptr
envoy_dynamic_module_on_header_formatter_config_new(
    envoy_dynamic_module_type_header_formatter_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config) {
  static int config_dummy = 0;
  return &config_dummy;
}

void envoy_dynamic_module_on_header_formatter_config_destroy(
    envoy_dynamic_module_type_header_formatter_config_module_ptr config_module_ptr) {}

envoy_dynamic_module_type_header_formatter_module_ptr envoy_dynamic_module_on_header_formatter_new(
    envoy_dynamic_module_type_header_formatter_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_header_formatter_envoy_ptr formatter_envoy_ptr) {
  // Nothing is exposed through the Envoy-side pointer yet, but Envoy must always hand one over.
  // Declining here makes the tests that expect a formatter fail if that ever regresses.
  if (formatter_envoy_ptr == NULL) {
    return NULL;
  }
  header_formatter* formatter = calloc(1, sizeof(header_formatter));
  return formatter;
}

void envoy_dynamic_module_on_header_formatter_destroy(
    envoy_dynamic_module_type_header_formatter_module_ptr formatter_module_ptr) {
  free((void*)formatter_module_ptr);
}

void envoy_dynamic_module_on_header_formatter_process_key(
    envoy_dynamic_module_type_header_formatter_envoy_ptr formatter_envoy_ptr,
    envoy_dynamic_module_type_header_formatter_module_ptr formatter_module_ptr,
    envoy_dynamic_module_type_envoy_buffer key) {
  header_formatter* formatter = (header_formatter*)formatter_module_ptr;
  // Every per-message hook is handed the Envoy-side formatter pointer. Nothing is exposed through
  // it yet, so the module only checks that it is there: dropping the key makes the tests that
  // expect the peer's casing restored fail if that ever regresses.
  if (formatter_envoy_ptr == NULL) {
    return;
  }
  if (formatter->key_count >= MAX_KEYS || key.length == 0 || key.length >= MAX_KEY_LEN) {
    return;
  }
  memcpy(formatter->keys[formatter->key_count], key.ptr, key.length);
  formatter->keys[formatter->key_count][key.length] = '\0';
  formatter->key_count++;
}

bool envoy_dynamic_module_on_header_formatter_format(
    envoy_dynamic_module_type_header_formatter_envoy_ptr formatter_envoy_ptr,
    envoy_dynamic_module_type_header_formatter_module_ptr formatter_module_ptr,
    envoy_dynamic_module_type_envoy_buffer key, envoy_dynamic_module_type_module_buffer* result) {
  header_formatter* formatter = (header_formatter*)formatter_module_ptr;
  if (formatter_envoy_ptr == NULL || key.length == 0 || key.length >= MAX_KEY_LEN) {
    return false;
  }

  for (size_t i = 0; i < formatter->key_count; i++) {
    if (strlen(formatter->keys[i]) == key.length &&
        strncasecmp(formatter->keys[i], key.ptr, key.length) == 0) {
      memcpy(formatter->formatted, formatter->keys[i], key.length);
      result->ptr = formatter->formatted;
      result->length = key.length;
      return true;
    }
  }

  // Not a key the peer sent: upper-case it so the test can tell the two paths apart.
  for (size_t i = 0; i < key.length; i++) {
    const char c = key.ptr[i];
    formatter->formatted[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
  }
  result->ptr = formatter->formatted;
  result->length = key.length;
  return true;
}
