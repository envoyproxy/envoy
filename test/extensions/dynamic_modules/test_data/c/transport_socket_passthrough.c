#include <assert.h>
#include <string.h>

#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/transport_socket_abi.h"

static int some_variable = 0;

envoy_dynamic_module_type_abi_version_module_ptr envoy_dynamic_module_on_program_init(void) {
  return envoy_dynamic_modules_abi_version;
}

envoy_dynamic_module_type_transport_socket_factory_config_module_ptr
envoy_dynamic_module_on_transport_socket_factory_config_new(
    envoy_dynamic_module_type_transport_socket_factory_config_envoy_ptr factory_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer socket_name,
    envoy_dynamic_module_type_envoy_buffer socket_config, bool is_upstream) {
  (void)factory_config_envoy_ptr;
  (void)socket_name;
  (void)socket_config;
  (void)is_upstream;
  return &some_variable;
}

void envoy_dynamic_module_on_transport_socket_factory_config_destroy(
    envoy_dynamic_module_type_transport_socket_factory_config_module_ptr factory_config_ptr) {
  assert(factory_config_ptr == &some_variable);
}

envoy_dynamic_module_type_transport_socket_module_ptr envoy_dynamic_module_on_transport_socket_new(
    envoy_dynamic_module_type_transport_socket_factory_config_module_ptr factory_config_ptr,
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr) {
  (void)factory_config_ptr;
  (void)transport_socket_envoy_ptr;
  return &some_variable + 1;
}

void envoy_dynamic_module_on_transport_socket_destroy(
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr) {
  assert(transport_socket_module_ptr == &some_variable + 1);
}

void envoy_dynamic_module_on_transport_socket_set_callbacks(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr) {
  (void)transport_socket_envoy_ptr;
  (void)transport_socket_module_ptr;
}

void envoy_dynamic_module_on_transport_socket_on_connected(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr) {
  (void)transport_socket_envoy_ptr;
  (void)transport_socket_module_ptr;
  // Raise connected event to signal that the transport is ready.
  envoy_dynamic_module_callback_transport_socket_raise_event(
      transport_socket_envoy_ptr, envoy_dynamic_module_type_network_connection_event_Connected);
}

envoy_dynamic_module_type_transport_socket_io_result
envoy_dynamic_module_on_transport_socket_do_read(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr) {
  (void)transport_socket_module_ptr;

  // Read from the io_handle and add to read buffer (passthrough behavior).
  void* io_handle =
      envoy_dynamic_module_callback_transport_socket_get_io_handle(transport_socket_envoy_ptr);

  char buf[16384];
  size_t bytes_read = 0;
  int64_t rc = envoy_dynamic_module_callback_transport_socket_io_handle_read(io_handle, buf,
                                                                             sizeof(buf),
                                                                             &bytes_read);

  envoy_dynamic_module_type_transport_socket_io_result result = {
      envoy_dynamic_module_type_transport_socket_post_io_action_KeepOpen, 0, false};

  if (rc != 0) {
    // I/O error. Check if it's a real error or EAGAIN.
    // EAGAIN (code 11 on Linux, or IoErrorCode::Again which is 0 in Envoy) means try again later.
    if (bytes_read == 0) {
      return result;
    }
    result.action = envoy_dynamic_module_type_transport_socket_post_io_action_Close;
    return result;
  }

  if (bytes_read == 0) {
    // End of stream.
    result.end_stream_read = true;
    return result;
  }

  // Add the read data to the read buffer.
  envoy_dynamic_module_callback_transport_socket_read_buffer_add(transport_socket_envoy_ptr, buf,
                                                                 bytes_read);
  result.bytes_processed = bytes_read;

  // Check if we should yield.
  if (envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer(
          transport_socket_envoy_ptr)) {
    envoy_dynamic_module_callback_transport_socket_set_is_readable(transport_socket_envoy_ptr);
  }

  return result;
}

envoy_dynamic_module_type_transport_socket_io_result
envoy_dynamic_module_on_transport_socket_do_write(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr,
    size_t write_buffer_length, bool end_stream) {
  (void)transport_socket_module_ptr;
  (void)end_stream;

  envoy_dynamic_module_type_transport_socket_io_result result = {
      envoy_dynamic_module_type_transport_socket_post_io_action_KeepOpen, 0, false};

  if (write_buffer_length == 0) {
    return result;
  }

  // Get write buffer slices and write them to the io_handle.
  void* io_handle =
      envoy_dynamic_module_callback_transport_socket_get_io_handle(transport_socket_envoy_ptr);

  envoy_dynamic_module_type_envoy_buffer slices[64];
  size_t slices_count = 64;
  envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(
      transport_socket_envoy_ptr, slices, &slices_count);

  size_t total_written = 0;
  for (size_t i = 0; i < slices_count; i++) {
    size_t bytes_written = 0;
    int64_t rc = envoy_dynamic_module_callback_transport_socket_io_handle_write(
        io_handle, slices[i].ptr, slices[i].length, &bytes_written);
    if (rc != 0) {
      break;
    }
    total_written += bytes_written;
    if (bytes_written < slices[i].length) {
      break;
    }
  }

  if (total_written > 0) {
    envoy_dynamic_module_callback_transport_socket_write_buffer_drain(transport_socket_envoy_ptr,
                                                                     total_written);
  }
  result.bytes_processed = total_written;
  return result;
}

void envoy_dynamic_module_on_transport_socket_close(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr,
    envoy_dynamic_module_type_network_connection_event event) {
  (void)transport_socket_envoy_ptr;
  (void)transport_socket_module_ptr;
  (void)event;
}

void envoy_dynamic_module_on_transport_socket_get_protocol(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr,
    envoy_dynamic_module_type_module_buffer* result) {
  (void)transport_socket_envoy_ptr;
  (void)transport_socket_module_ptr;
  result->ptr = NULL;
  result->length = 0;
}

void envoy_dynamic_module_on_transport_socket_get_failure_reason(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr,
    envoy_dynamic_module_type_module_buffer* result) {
  (void)transport_socket_envoy_ptr;
  (void)transport_socket_module_ptr;
  result->ptr = NULL;
  result->length = 0;
}

bool envoy_dynamic_module_on_transport_socket_can_flush_close(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr) {
  (void)transport_socket_envoy_ptr;
  (void)transport_socket_module_ptr;
  return true;
}

