#pragma once

// NOLINT(namespace-envoy)

// This is a pure C header file that defines the ABI of transport socket dynamic modules.
// It must not contain any dependencies besides standard library since it is used by
// both Envoy and dynamic module SDKs written in non-C++ languages.

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

// -----------------------------------------------------------------------------
// ---------------------------------- Types ------------------------------------
// -----------------------------------------------------------------------------

/**
 * envoy_dynamic_module_type_transport_socket_config_envoy_ptr is a raw pointer to
 * the DynamicModuleTransportSocketConfig class in Envoy.
 *
 * OWNERSHIP: Envoy owns the pointer.
 */
typedef void* envoy_dynamic_module_type_transport_socket_config_envoy_ptr;

/**
 * envoy_dynamic_module_type_transport_socket_config_module_ptr is a pointer to an in-module
 * transport socket configuration.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer.
 */
typedef const void* envoy_dynamic_module_type_transport_socket_config_module_ptr;

/**
 * envoy_dynamic_module_type_transport_socket_envoy_ptr is a raw pointer to
 * the DynamicModuleTransportSocket class in Envoy.
 *
 * OWNERSHIP: Envoy owns the pointer.
 */
typedef void* envoy_dynamic_module_type_transport_socket_envoy_ptr;

/**
 * envoy_dynamic_module_type_transport_socket_module_ptr is a pointer to an in-module
 * transport socket instance.
 *
 * OWNERSHIP: The module is responsible for managing the lifetime of the pointer.
 */
typedef const void* envoy_dynamic_module_type_transport_socket_module_ptr;

/**
 * envoy_dynamic_module_type_connection_event represents connection events.
 * This corresponds to Network::ConnectionEvent in Envoy.
 */
typedef enum {
  envoy_dynamic_module_type_connection_event_RemoteClose = 0,
  envoy_dynamic_module_type_connection_event_LocalClose = 1,
  envoy_dynamic_module_type_connection_event_Connected = 2,
  envoy_dynamic_module_type_connection_event_ConnectedZeroRtt = 3,
} envoy_dynamic_module_type_connection_event;

/**
 * envoy_dynamic_module_type_post_io_action represents the action after an I/O operation.
 * This corresponds to Network::PostIoAction in Envoy.
 */
typedef enum {
  envoy_dynamic_module_type_post_io_action_KeepOpen = 0,
  envoy_dynamic_module_type_post_io_action_Close = 1,
} envoy_dynamic_module_type_post_io_action;

/**
 * envoy_dynamic_module_type_io_result represents the result of an I/O operation.
 * This corresponds to Network::IoResult in Envoy.
 */
typedef struct {
  envoy_dynamic_module_type_post_io_action action;
  uint64_t bytes_processed;
  bool end_stream_read;
} envoy_dynamic_module_type_io_result;

/**
 * envoy_dynamic_module_type_ssl_connection_info represents SSL connection information.
 */
typedef struct {
  const char* protocol_ptr;
  size_t protocol_length;
  const char* server_name_ptr;
  size_t server_name_length;
  const char* cipher_suite_ptr;
  size_t cipher_suite_length;
  const char* tls_version_ptr;
  size_t tls_version_length;
  const char* peer_certificate_ptr;
  size_t peer_certificate_length;
  const char* peer_certificate_chain_ptr;
  size_t peer_certificate_chain_length;
  bool peer_certificate_validated;
} envoy_dynamic_module_type_ssl_connection_info;

// -----------------------------------------------------------------------------
// ------------------------------- Event Hooks ---------------------------------
// -----------------------------------------------------------------------------

/**
 * envoy_dynamic_module_on_transport_socket_config_new is called by the main thread when
 * a transport socket configuration is loaded.
 *
 * @param config_envoy_ptr is the pointer to the DynamicModuleTransportSocketConfig object.
 * @param name_ptr is the name of the transport socket.
 * @param name_size is the size of the name.
 * @param config_ptr is the configuration for the module.
 * @param config_size is the size of the configuration.
 * @param is_upstream true if this is an upstream socket, false for downstream.
 * @return envoy_dynamic_module_type_transport_socket_config_module_ptr is the pointer to the
 * in-module transport socket configuration. Returning nullptr indicates a failure.
 */
envoy_dynamic_module_type_transport_socket_config_module_ptr
envoy_dynamic_module_on_transport_socket_config_new(
    envoy_dynamic_module_type_transport_socket_config_envoy_ptr config_envoy_ptr,
    const char* name_ptr, size_t name_size, const char* config_ptr, size_t config_size,
    bool is_upstream);

/**
 * envoy_dynamic_module_on_transport_socket_config_destroy is called when a transport socket
 * configuration is destroyed.
 *
 * @param config_ptr is a pointer to the in-module transport socket configuration.
 */
void envoy_dynamic_module_on_transport_socket_config_destroy(
    envoy_dynamic_module_type_transport_socket_config_module_ptr config_ptr);

/**
 * envoy_dynamic_module_on_transport_socket_new is called when a new transport socket is created.
 *
 * @param config_ptr is the pointer to the in-module transport socket configuration.
 * @param socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 * @return envoy_dynamic_module_type_transport_socket_module_ptr is the pointer to the in-module
 * transport socket. Returning nullptr indicates a failure.
 */
envoy_dynamic_module_type_transport_socket_module_ptr envoy_dynamic_module_on_transport_socket_new(
    envoy_dynamic_module_type_transport_socket_config_module_ptr config_ptr,
    envoy_dynamic_module_type_transport_socket_envoy_ptr socket_envoy_ptr);

/**
 * envoy_dynamic_module_on_transport_socket_set_callbacks is called to set transport socket
 * callbacks.
 *
 * @param socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 * @param socket_module_ptr is the pointer to the in-module transport socket.
 */
void envoy_dynamic_module_on_transport_socket_set_callbacks(
    envoy_dynamic_module_type_transport_socket_envoy_ptr socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr socket_module_ptr);

/**
 * envoy_dynamic_module_on_transport_socket_protocol is called to get the negotiated protocol.
 *
 * @param socket_module_ptr is the pointer to the in-module transport socket.
 * @param protocol_ptr is the pointer to store the protocol string.
 * @param protocol_length is the pointer to store the protocol length.
 */
void envoy_dynamic_module_on_transport_socket_protocol(
    envoy_dynamic_module_type_transport_socket_module_ptr socket_module_ptr,
    const char** protocol_ptr, size_t* protocol_length);

/**
 * envoy_dynamic_module_on_transport_socket_failure_reason is called to get the failure reason.
 *
 * @param socket_module_ptr is the pointer to the in-module transport socket.
 * @param reason_ptr is the pointer to store the failure reason string.
 * @param reason_length is the pointer to store the reason length.
 */
void envoy_dynamic_module_on_transport_socket_failure_reason(
    envoy_dynamic_module_type_transport_socket_module_ptr socket_module_ptr,
    const char** reason_ptr, size_t* reason_length);

/**
 * envoy_dynamic_module_on_transport_socket_can_flush_close is called to check if the socket
 * can be flushed and closed.
 *
 * @param socket_module_ptr is the pointer to the in-module transport socket.
 * @return true if the socket can be flushed and closed.
 */
bool envoy_dynamic_module_on_transport_socket_can_flush_close(
    envoy_dynamic_module_type_transport_socket_module_ptr socket_module_ptr);

/**
 * envoy_dynamic_module_on_transport_socket_close is called when the socket is closed.
 *
 * @param socket_module_ptr is the pointer to the in-module transport socket.
 * @param event the connection close event.
 */
void envoy_dynamic_module_on_transport_socket_close(
    envoy_dynamic_module_type_transport_socket_module_ptr socket_module_ptr,
    envoy_dynamic_module_type_connection_event event);

/**
 * envoy_dynamic_module_on_transport_socket_on_connected is called when the connection
 * is established.
 *
 * @param socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 * @param socket_module_ptr is the pointer to the in-module transport socket.
 */
void envoy_dynamic_module_on_transport_socket_on_connected(
    envoy_dynamic_module_type_transport_socket_envoy_ptr socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr socket_module_ptr);

/**
 * envoy_dynamic_module_on_transport_socket_do_read is called to read data from the socket.
 *
 * @param socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 * @param socket_module_ptr is the pointer to the in-module transport socket.
 * @param buffer_ptr is the buffer to read into.
 * @param buffer_capacity is the capacity of the buffer.
 * @return envoy_dynamic_module_type_io_result the result of the read operation.
 */
envoy_dynamic_module_type_io_result envoy_dynamic_module_on_transport_socket_do_read(
    envoy_dynamic_module_type_transport_socket_envoy_ptr socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr socket_module_ptr, void* buffer_ptr,
    size_t buffer_capacity);

/**
 * envoy_dynamic_module_on_transport_socket_do_write is called to write data to the socket.
 *
 * @param socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 * @param socket_module_ptr is the pointer to the in-module transport socket.
 * @param buffer_ptr is the buffer to write from.
 * @param buffer_length is the length of the data to write.
 * @param end_stream true if this is the end of the stream.
 * @return envoy_dynamic_module_type_io_result the result of the write operation.
 */
envoy_dynamic_module_type_io_result envoy_dynamic_module_on_transport_socket_do_write(
    envoy_dynamic_module_type_transport_socket_envoy_ptr socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr socket_module_ptr, const void* buffer_ptr,
    size_t buffer_length, bool end_stream);

/**
 * envoy_dynamic_module_on_transport_socket_start_secure_transport is called to start
 * secure transport (e.g., TLS handshake).
 *
 * @param socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 * @param socket_module_ptr is the pointer to the in-module transport socket.
 * @return true if secure transport was successfully started.
 */
bool envoy_dynamic_module_on_transport_socket_start_secure_transport(
    envoy_dynamic_module_type_transport_socket_envoy_ptr socket_envoy_ptr,
    envoy_dynamic_module_type_transport_socket_module_ptr socket_module_ptr);

/**
 * envoy_dynamic_module_on_transport_socket_configure_initial_congestion_window is called to
 * configure the initial congestion window.
 *
 * @param socket_module_ptr is the pointer to the in-module transport socket.
 * @param bandwidth_bits_per_sec the bandwidth in bits per second.
 * @param rtt_usec the round-trip time in microseconds.
 */
void envoy_dynamic_module_on_transport_socket_configure_initial_congestion_window(
    envoy_dynamic_module_type_transport_socket_module_ptr socket_module_ptr,
    uint64_t bandwidth_bits_per_sec, uint64_t rtt_usec);

/**
 * envoy_dynamic_module_on_transport_socket_get_ssl_info is called to get SSL connection info.
 *
 * @param socket_module_ptr is the pointer to the in-module transport socket.
 * @param info_ptr is the pointer to store the SSL connection info.
 * @return true if SSL info is available.
 */
bool envoy_dynamic_module_on_transport_socket_get_ssl_info(
    envoy_dynamic_module_type_transport_socket_module_ptr socket_module_ptr,
    envoy_dynamic_module_type_ssl_connection_info* info_ptr);

/**
 * envoy_dynamic_module_on_transport_socket_destroy is called when a transport socket is destroyed.
 *
 * @param socket_module_ptr is the pointer to the in-module transport socket.
 */
void envoy_dynamic_module_on_transport_socket_destroy(
    envoy_dynamic_module_type_transport_socket_module_ptr socket_module_ptr);

// -----------------------------------------------------------------------------
// -------------------------------- Callbacks ----------------------------------
// -----------------------------------------------------------------------------

/**
 * envoy_dynamic_module_callback_transport_socket_get_io_handle_fd is called to get the file
 * descriptor of the underlying I/O handle.
 *
 * @param socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 * @return the file descriptor, or -1 if not available.
 */
int envoy_dynamic_module_callback_transport_socket_get_io_handle_fd(
    envoy_dynamic_module_type_transport_socket_envoy_ptr socket_envoy_ptr);

/**
 * envoy_dynamic_module_callback_transport_socket_io_handle_read is called to read data from
 * the underlying I/O handle.
 *
 * @param socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 * @param buffer_ptr is the buffer to read into.
 * @param buffer_capacity is the capacity of the buffer.
 * @param bytes_read is the pointer to store the number of bytes read.
 * @return 0 on success, -1 on error with errno set, -2 on EAGAIN/EWOULDBLOCK.
 */
int envoy_dynamic_module_callback_transport_socket_io_handle_read(
    envoy_dynamic_module_type_transport_socket_envoy_ptr socket_envoy_ptr, void* buffer_ptr,
    size_t buffer_capacity, uint64_t* bytes_read);

/**
 * envoy_dynamic_module_callback_transport_socket_io_handle_write is called to write data to
 * the underlying I/O handle.
 *
 * @param socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 * @param buffer_ptr is the buffer to write from.
 * @param buffer_length is the length of the data to write.
 * @param bytes_written is the pointer to store the number of bytes written.
 * @return 0 on success, -1 on error with errno set, -2 on EAGAIN/EWOULDBLOCK.
 */
int envoy_dynamic_module_callback_transport_socket_io_handle_write(
    envoy_dynamic_module_type_transport_socket_envoy_ptr socket_envoy_ptr, const void* buffer_ptr,
    size_t buffer_length, uint64_t* bytes_written);

/**
 * envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer is called to check
 * if the read buffer should be drained.
 *
 * @param socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 * @return true if the read buffer should be drained.
 */
bool envoy_dynamic_module_callback_transport_socket_should_drain_read_buffer(
    envoy_dynamic_module_type_transport_socket_envoy_ptr socket_envoy_ptr);

/**
 * envoy_dynamic_module_callback_transport_socket_set_readable is called to mark the transport
 * socket as readable.
 *
 * @param socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 */
void envoy_dynamic_module_callback_transport_socket_set_readable(
    envoy_dynamic_module_type_transport_socket_envoy_ptr socket_envoy_ptr);

/**
 * envoy_dynamic_module_callback_transport_socket_raise_event is called to raise a connection event.
 *
 * @param socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 * @param event the connection event to raise.
 */
void envoy_dynamic_module_callback_transport_socket_raise_event(
    envoy_dynamic_module_type_transport_socket_envoy_ptr socket_envoy_ptr,
    envoy_dynamic_module_type_connection_event event);

/**
 * envoy_dynamic_module_callback_transport_socket_flush_write_buffer is called to flush the
 * write buffer.
 *
 * @param socket_envoy_ptr is the pointer to the DynamicModuleTransportSocket object.
 */
void envoy_dynamic_module_callback_transport_socket_flush_write_buffer(
    envoy_dynamic_module_type_transport_socket_envoy_ptr socket_envoy_ptr);

/**
 * envoy_dynamic_module_callback_transport_socket_log is called to log a message.
 *
 * @param level is the log level (0=trace, 1=debug, 2=info, 3=warn, 4=error, 5=critical).
 * @param message_ptr is the pointer to the message to be logged.
 * @param message_length is the length of the message.
 */
void envoy_dynamic_module_callback_transport_socket_log(int level, const char* message_ptr,
                                                        size_t message_length);

#ifdef __cplusplus
}
#endif
