#pragma once

// NOLINT(namespace-envoy)

// This is a pure C header, so we can't apply clang-tidy to it.
// NOLINTBEGIN

// This is a pure C header file that defines the ABI of the transport socket dynamic modules used
// by Envoy.
//
// This must not contain any dependencies besides standard library since it is not only used by
// Envoy itself but also by dynamic module SDKs written in non-C++ languages.
//
// There are three kinds defined in this file:
//
//  * Types: type definitions used in the ABI.
//  * Events Hooks: functions that modules must implement to handle events from Envoy.
//  * Callbacks: functions that Envoy implements and modules can call to interact with Envoy.
//
// Types are prefixed with "envoy_dynamic_module_type_". Event Hooks are prefixed with
// "envoy_dynamic_module_on_". Callbacks are prefixed with "envoy_dynamic_module_callback_".

#ifdef __cplusplus
#include <cstdbool>
#include <cstddef>
#include <cstdint>

extern "C" {
#else

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif

// Include the common ABI types.
// When built from the SDK, the path is relative to this file.
// When built from Envoy, the include path is set correctly.
#if defined(__ENVOY__)
#include "source/extensions/dynamic_modules/abi/abi.h"
#else
#include "abi/abi.h"
#endif

// -----------------------------------------------------------------------------
// ---------------------------------- Types ------------------------------------
// -----------------------------------------------------------------------------

/**
 * envoy_dynamic_module_type_transport_socket_factory_config_envoy_ptr is a raw pointer to
 * the DynamicModuleTransportSocketFactoryConfig class in Envoy. This is passed to the module when
 * creating a new in-module transport socket factory configuration and used to access factory-scoped
 * information.
 *
 * This has 1:1 correspondence with
 * envoy_dynamic_module_type_transport_socket_factory_config_module_ptr in the module.
 *
 * OWNERSHIP: Envoy owns the pointer.
 */
typedef void* envoy_dynamic_module_type_transport_socket_factory_config_envoy_ptr;

/**
 * envoy_dynamic_module_type_transport_socket_factory_config_module_ptr is a pointer to an
 * in-module transport socket factory configuration corresponding to an Envoy transport socket
 * factory configuration. The factory config is responsible for creating new transport sockets.
 *
 * This has 1:1 correspondence with the DynamicModuleTransportSocketFactoryConfig class in Envoy.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer. The pointer can be
 * released when envoy_dynamic_module_on_transport_socket_factory_config_destroy is called for the
 * same pointer.
 */
typedef const void* envoy_dynamic_module_type_transport_socket_factory_config_module_ptr;

/**
 * envoy_dynamic_module_type_transport_socket_envoy_ptr is a raw pointer to the
 * DynamicModuleTransportSocket class in Envoy. This is passed to the module when creating a new
 * transport socket for each connection and used to access the transport socket-scoped information
 * such as buffers, connection data, etc.
 *
 * This has 1:1 correspondence with envoy_dynamic_module_type_transport_socket_module_ptr in the
 * module.
 *
 * OWNERSHIP: Envoy owns the pointer, and can be accessed by the module until the transport socket
 * is destroyed, i.e. envoy_dynamic_module_on_transport_socket_destroy is called.
 */
typedef void* envoy_dynamic_module_type_transport_socket_envoy_ptr;

/**
 * envoy_dynamic_module_type_transport_socket_module_ptr is a pointer to an in-module transport
 * socket corresponding to an Envoy transport socket. The transport socket is responsible for
 * handling read/write operations on the connection.
 *
 * This has 1:1 correspondence with the DynamicModuleTransportSocket class in Envoy.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer. The pointer can be
 * released when envoy_dynamic_module_on_transport_socket_destroy is called for the same pointer.
 */
typedef const void* envoy_dynamic_module_type_transport_socket_module_ptr;

/**
 * envoy_dynamic_module_type_transport_socket_post_io_action represents the action to take after
 * an I/O operation. This corresponds to `Network::PostIoAction` in envoy/network/post_io_action.h.
 */
typedef enum envoy_dynamic_module_type_transport_socket_post_io_action {
  // Keep the socket open.
  envoy_dynamic_module_type_transport_socket_post_io_action_KeepOpen,
  // Close the socket.
  envoy_dynamic_module_type_transport_socket_post_io_action_Close,
} envoy_dynamic_module_type_transport_socket_post_io_action;

/**
 * envoy_dynamic_module_type_transport_socket_io_result represents the result of an I/O operation.
 * This corresponds to `Network::IoResult` in envoy/network/transport_socket.h.
 */
typedef struct envoy_dynamic_module_type_transport_socket_io_result {
  // The action to take after the I/O operation.
  envoy_dynamic_module_type_transport_socket_post_io_action action;
  // Number of bytes processed by the I/O event.
  uint64_t bytes_processed;
  // True if an end-of-stream was read from a connection. This can only be true for read operations.
  bool end_stream_read;
} envoy_dynamic_module_type_transport_socket_io_result;

// -----------------------------------------------------------------------------
// ------------------------------- Event Hooks ---------------------------------
// -----------------------------------------------------------------------------

/**
 * envoy_dynamic_module_on_transport_socket_factory_config_new is called by the main thread when the
 * transport socket factory config is loaded. The function returns a
 * envoy_dynamic_module_type_transport_socket_factory_config_module_ptr for given name and config.
 *
 * @param factory_config_envoy_ptr is the pointer to the DynamicModuleTransportSocketFactoryConfig
 * object for the corresponding config.
 * @param socket_name is the name of the transport socket implementation.
 * @param socket_config is the configuration for the transport socket.
 * @param is_upstream is true if this is for upstream connections, false for downstream.
 * @return envoy_dynamic_module_type_transport_socket_factory_config_module_ptr is the pointer to
 * the in-module transport socket factory configuration. Returning nullptr indicates a failure to
 * initialize the module. When it fails, the factory configuration will be rejected.
 */
envoy_dynamic_module_type_transport_socket_factory_config_module_ptr
envoy_dynamic_module_on_transport_socket_factory_config_new(
    envoy_dynamic_module_type_transport_socket_factory_config_envoy_ptr factory_config_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer socket_name,
    envoy_dynamic_module_type_envoy_buffer socket_config, bool is_upstream);

/**
 * envoy_dynamic_module_on_transport_socket_factory_config_destroy is called when the transport
 * socket factory configuration is destroyed in Envoy. The module should release any resources
 * associated with the corresponding in-module transport socket factory configuration.
 *
 * @param factory_config_ptr is a pointer to the in-module transport socket factory configuration
 * whose corresponding Envoy transport socket factory configuration is being destroyed.
 */
void envoy_dynamic_module_on_transport_socket_factory_config_destroy(
    envoy_dynamic_module_type_transport_socket_factory_config_module_ptr factory_config_ptr);

/**
 * envoy_dynamic_module_on_transport_socket_new is called when a new transport socket is created for
 * a connection.
 *
 * @param factory_config_ptr is the pointer to the in-module transport socket factory configuration.
 * @param transport_socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object of
 * the corresponding transport socket.
 * @return envoy_dynamic_module_type_transport_socket_module_ptr is the pointer to the in-module
 * transport socket. Returning nullptr indicates a failure to initialize the module. When it fails,
 * the connection will be closed.
 */
envoy_dynamic_module_type_transport_socket_module_ptr envoy_dynamic_module_on_transport_socket_new(
    envoy_dynamic_module_type_transport_socket_factory_config_module_ptr factory_config_ptr,
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr);

/**
 * envoy_dynamic_module_on_transport_socket_destroy is called when the transport socket is
 * destroyed.
 *
 * @param transport_socket_module_ptr is the pointer to the in-module transport socket.
 */
void envoy_dynamic_module_on_transport_socket_destroy(
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr);

/**
 * envoy_dynamic_module_on_transport_socket_set_callbacks is called when the transport socket
 * callbacks are set. This is called before any I/O operations.
 *
 * @param transport_socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 * @param transport_socket_module_ptr is the pointer to the in-module transport socket.
 */
void envoy_dynamic_module_on_transport_socket_set_callbacks(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr);

/**
 * envoy_dynamic_module_on_transport_socket_on_connected is called when the underlying transport is
 * established.
 *
 * @param transport_socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 * @param transport_socket_module_ptr is the pointer to the in-module transport socket.
 */
void envoy_dynamic_module_on_transport_socket_on_connected(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr);

/**
 * envoy_dynamic_module_on_transport_socket_do_read is called when data is to be read from the
 * connection.
 *
 * @param transport_socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 * @param transport_socket_module_ptr is the pointer to the in-module transport socket.
 * @return envoy_dynamic_module_type_transport_socket_io_result is the result of the read operation.
 */
envoy_dynamic_module_type_transport_socket_io_result
envoy_dynamic_module_on_transport_socket_do_read(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr);

/**
 * envoy_dynamic_module_on_transport_socket_do_write is called when data is to be written to the
 * connection.
 *
 * @param transport_socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 * @param transport_socket_module_ptr is the pointer to the in-module transport socket.
 * @param write_buffer_length is the length of the write buffer.
 * @param end_stream indicates if this is the end of the stream.
 * @return envoy_dynamic_module_type_transport_socket_io_result is the result of the write
 * operation.
 */
envoy_dynamic_module_type_transport_socket_io_result
envoy_dynamic_module_on_transport_socket_do_write(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr,
    size_t write_buffer_length, bool end_stream);

/**
 * envoy_dynamic_module_on_transport_socket_close is called when the socket is being closed.
 *
 * @param transport_socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 * @param transport_socket_module_ptr is the pointer to the in-module transport socket.
 * @param event is the connection event that caused the close.
 */
void envoy_dynamic_module_on_transport_socket_close(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr,
    envoy_dynamic_module_type_network_connection_event event);

/**
 * envoy_dynamic_module_on_transport_socket_get_protocol is called to get the negotiated protocol.
 *
 * @param transport_socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 * @param transport_socket_module_ptr is the pointer to the in-module transport socket.
 * @param result is the buffer to write the protocol string to. The module sets this to the protocol
 * string.
 */
void envoy_dynamic_module_on_transport_socket_get_protocol(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr,
    envoy_dynamic_module_type_module_buffer* result);

/**
 * envoy_dynamic_module_on_transport_socket_get_failure_reason is called to get the failure reason.
 *
 * @param transport_socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 * @param transport_socket_module_ptr is the pointer to the in-module transport socket.
 * @param result is the buffer to write the failure reason string to. The module sets this to the
 * failure reason string.
 */
void envoy_dynamic_module_on_transport_socket_get_failure_reason(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr,
    envoy_dynamic_module_type_module_buffer* result);

/**
 * envoy_dynamic_module_on_transport_socket_can_flush_close is called to check if the socket can be
 * flushed and closed.
 *
 * @param transport_socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 * @param transport_socket_module_ptr is the pointer to the in-module transport socket.
 * @return true if the socket can be flushed and closed, false otherwise.
 */
bool envoy_dynamic_module_on_transport_socket_can_flush_close(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr transport_socket_module_ptr);

// -----------------------------------------------------------------------------
// -------------------------------- Callbacks ----------------------------------
// -----------------------------------------------------------------------------

/**
 * envoy_dynamic_module_callback_transport_socket_get_io_handle provides access to the I/O handle
 * for performing raw I/O operations on the connection.
 *
 * @param transport_socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 * @return an opaque handle to the I/O handle. This is used with the raw I/O callbacks.
 */
void* envoy_dynamic_module_callback_transport_socket_get_io_handle(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr);

/**
 * envoy_dynamic_module_callback_transport_socket_io_handle_read reads data from the raw socket.
 *
 * @param io_handle is the opaque handle returned by
 * envoy_dynamic_module_callback_transport_socket_get_io_handle.
 * @param buffer is the buffer to read into.
 * @param length is the maximum number of bytes to read.
 * @param bytes_read is set to the number of bytes actually read.
 * @return the I/O error code, or 0 for success.
 */
int64_t envoy_dynamic_module_callback_transport_socket_io_handle_read(void* io_handle, char* buffer,
                                                                      size_t length,
                                                                      size_t* bytes_read);

/**
 * envoy_dynamic_module_callback_transport_socket_io_handle_write writes data to the raw socket.
 *
 * @param io_handle is the opaque handle returned by
 * envoy_dynamic_module_callback_transport_socket_get_io_handle.
 * @param buffer is the buffer to write from.
 * @param length is the number of bytes to write.
 * @param bytes_written is set to the number of bytes actually written.
 * @return the I/O error code, or 0 for success.
 */
int64_t envoy_dynamic_module_callback_transport_socket_io_handle_write(void* io_handle,
                                                                       const char* buffer,
                                                                       size_t length,
                                                                       size_t* bytes_written);

/**
 * envoy_dynamic_module_callback_transport_socket_read_buffer_drain drains bytes from the read
 * buffer.
 *
 * @param transport_socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 * @param length is the number of bytes to drain.
 */
void envoy_dynamic_module_callback_transport_socket_read_buffer_drain(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr, size_t length);

/**
 * envoy_dynamic_module_callback_transport_socket_read_buffer_add adds data to the read buffer.
 *
 * @param transport_socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 * @param data is the data to add.
 * @param length is the length of the data.
 */
void envoy_dynamic_module_callback_transport_socket_read_buffer_add(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    const char* data, size_t length);

/**
 * envoy_dynamic_module_callback_transport_socket_read_buffer_length returns the length of the read
 * buffer.
 *
 * @param transport_socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 * @return the length of the read buffer.
 */
size_t envoy_dynamic_module_callback_transport_socket_read_buffer_length(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr);

/**
 * envoy_dynamic_module_callback_transport_socket_write_buffer_drain drains bytes from the write
 * buffer.
 *
 * @param transport_socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 * @param length is the number of bytes to drain.
 */
void envoy_dynamic_module_callback_transport_socket_write_buffer_drain(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr, size_t length);

/**
 * envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices gets the slices of the
 * write buffer. If slices is NULL, the function returns the total number of slices in slices_count
 * without copying (query mode). Otherwise, it copies up to slices_count slices into the output
 * array.
 *
 * @param transport_socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 * @param slices is the output array of slices, or NULL to query the number of slices.
 * @param slices_count is the maximum number of slices to return and will be set to the actual
 * number of slices returned (or total available when slices is NULL).
 */
void envoy_dynamic_module_callback_transport_socket_write_buffer_get_slices(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_envoy_buffer* slices, size_t* slices_count);

/**
 * envoy_dynamic_module_callback_transport_socket_write_buffer_length returns the length of the
 * write buffer.
 *
 * @param transport_socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 * @return the length of the write buffer.
 */
size_t envoy_dynamic_module_callback_transport_socket_write_buffer_length(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr);

/**
 * envoy_dynamic_module_callback_transport_socket_raise_event raises a connection event.
 *
 * @param transport_socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 * @param event is the connection event to raise.
 */
void envoy_dynamic_module_callback_transport_socket_raise_event(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr,
    envoy_dynamic_module_type_network_connection_event event);

/**
 * envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer checks if the read buffer
 * should be drained.
 *
 * @param transport_socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 * @return true if the read buffer should be drained, false otherwise.
 */
bool envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr);

/**
 * envoy_dynamic_module_callback_transport_socket_set_is_readable marks the transport socket as
 * readable.
 *
 * @param transport_socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 */
void envoy_dynamic_module_callback_transport_socket_set_is_readable(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr);

/**
 * envoy_dynamic_module_callback_transport_socket_flush_write_buffer flushes the write buffer.
 *
 * @param transport_socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 */
void envoy_dynamic_module_callback_transport_socket_flush_write_buffer(
    envoy_dynamic_module_type_transport_socket_envoy_ptr transport_socket_envoy_ptr);

#ifdef __cplusplus
}
#endif

// NOLINTEND
