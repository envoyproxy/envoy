#include <stdlib.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi/abi.h"

// A local address selector whose behavior is picked by the selector name:
//  - "default": continue without setting an address, which keeps the default selection.
//  - "fail": fail the connection.
//  - "select": use the IP address in the selector config, with port 0.
//  - "echo_endpoint": use the endpoint address with the endpoint port plus one.
//  - "expect_cluster": reject the config unless the cluster name equals the selector config.
//  - "reject": reject the config.

typedef enum { Default, Fail, Select, EchoEndpoint } mode_t_;

typedef struct {
  mode_t_ mode;
  char address[64];
} selector_config;

static int buffer_equals(envoy_dynamic_module_type_envoy_buffer buffer, const char* value) {
  return buffer.length == strlen(value) && memcmp(buffer.ptr, value, buffer.length) == 0;
}

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_local_address_selector_config_module_ptr
envoy_dynamic_module_on_local_address_selector_config_new(
    envoy_dynamic_module_type_local_address_selector_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer name, envoy_dynamic_module_type_envoy_buffer config,
    envoy_dynamic_module_type_envoy_buffer cluster_name) {
  (void)config_envoy_ptr;
  if (buffer_equals(name, "reject")) {
    return NULL;
  }
  if (buffer_equals(name, "expect_cluster") &&
      (cluster_name.length != config.length ||
       memcmp(cluster_name.ptr, config.ptr, config.length) != 0)) {
    return NULL;
  }
  if (config.length >= sizeof(((selector_config*)0)->address)) {
    return NULL;
  }

  selector_config* selector = calloc(1, sizeof(selector_config));
  if (buffer_equals(name, "fail")) {
    selector->mode = Fail;
  } else if (buffer_equals(name, "select")) {
    selector->mode = Select;
  } else if (buffer_equals(name, "echo_endpoint")) {
    selector->mode = EchoEndpoint;
  } else {
    selector->mode = Default;
  }
  if (config.length > 0) {
    memcpy(selector->address, config.ptr, config.length);
  }
  return selector;
}

void envoy_dynamic_module_on_local_address_selector_config_destroy(
    envoy_dynamic_module_type_local_address_selector_config_module_ptr config_module_ptr) {
  free((void*)config_module_ptr);
}

bool envoy_dynamic_module_on_local_address_selector_select(
    envoy_dynamic_module_type_local_address_selector_config_module_ptr config_module_ptr,
    envoy_dynamic_module_type_local_address_selector_envoy_ptr selector_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer endpoint_address, uint32_t endpoint_port) {
  const selector_config* selector = config_module_ptr;
  switch (selector->mode) {
  case Default:
    return true;
  case Fail:
    return false;
  case Select: {
    envoy_dynamic_module_type_module_buffer address = {selector->address,
                                                       strlen(selector->address)};
    return envoy_dynamic_module_callback_local_address_selector_set_address(selector_envoy_ptr,
                                                                            address, 0);
  }
  case EchoEndpoint: {
    envoy_dynamic_module_type_module_buffer address = {endpoint_address.ptr,
                                                       endpoint_address.length};
    return envoy_dynamic_module_callback_local_address_selector_set_address(
        selector_envoy_ptr, address, endpoint_port + 1);
  }
  }
  return false;
}
