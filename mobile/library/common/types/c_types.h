#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

// NOLINT(namespace-envoy)

/**
 * Handle to an Envoy engine instance. Valid only for the lifetime of the engine and not intended
 * for any external interpretation or use.
 */
typedef intptr_t envoy_engine_t;

/**
 * Handle to an outstanding Envoy HTTP stream. Valid only for the duration of the stream and not
 * intended for any external interpretation or use.
 */
typedef intptr_t envoy_stream_t;

/**
 * Result codes returned by all calls made to this interface.
 */
typedef enum { ENVOY_SUCCESS, ENVOY_FAILURE } envoy_status_t;

/**
 * Error code associated with terminal status of a HTTP stream.
 */
typedef enum {
  ENVOY_UNDEFINED_ERROR,
  ENVOY_STREAM_RESET,
  ENVOY_CONNECTION_FAILURE
} envoy_error_code_t;

/**
 * Networks classified by last physical link.
 * ENVOY_NET_GENERIC is default and includes cases where network characteristics are unknown.
 * ENVOY_NET_WLAN includes WiFi and other local area wireless networks.
 * ENVOY_NET_WWAN includes all mobile phone networks.
 */
typedef enum { ENVOY_NET_GENERIC, ENVOY_NET_WLAN, ENVOY_NET_WWAN } envoy_network_t;

#ifdef __cplusplus
extern "C" { // release function
#endif
/**
 * Callback indicating Envoy has drained the associated buffer.
 */
typedef void (*envoy_release_f)(void* context);

/**
 * No-op callback.
 */
void envoy_noop_release(void* context);

#ifdef __cplusplus
} // release function
#endif

/**
 * Holds raw binary data as an array of bytes.
 */
typedef struct {
  size_t length;
  const uint8_t* bytes;
  envoy_release_f release;
  void* context;
} envoy_data;

/**
 * Holds a single key/value header.
 */
typedef struct {
  envoy_data key;
  // Multiple header values for the same header key are supported via a comma-delimited string.
  envoy_data value;
} envoy_header;

/**
 * Consistent type for dealing with encodable/processable header counts.
 */
typedef int envoy_header_size_t;

/**
 * Holds an HTTP header map as an array of envoy_header structs.
 */
typedef struct {
  // Number of header elements in the array.
  envoy_header_size_t length;
  // Array of headers.
  envoy_header* headers;
} envoy_headers;

#ifdef __cplusplus
extern "C" { // utility functions
#endif

/**
 * malloc wrapper that asserts that the returned pointer is valid. Otherwise, the program exits.
 * @param size, the size of memory to be allocated in bytes.
 * @return void*, pointer to the allocated memory.
 */
void* safe_malloc(size_t size);

/**
 * Helper function to free/release memory associated with underlying headers.
 * @param headers, envoy_headers to release.
 */
void release_envoy_headers(envoy_headers headers);

/**
 * Helper function to copy envoy_headers.
 * @param src, the envoy_headers to copy from.
 * @param envoy_headers, copied headers.
 */
envoy_headers copy_envoy_headers(envoy_headers src);

/**
 * Helper function to copy envoy_data.
 * @param length, the length of the data to copy.
 * @param src_bytes, the byte array to copy from.
 * @return envoy_data, the envoy_data copied from the src.
 */
envoy_data copy_envoy_data(size_t length, const uint8_t* src_bytes);

#ifdef __cplusplus
} // utility functions
#endif

// Convenience constant to pass to function calls with no data.
// For example when sending a headers-only request.
extern const envoy_data envoy_nodata;

/**
 * Error struct.
 */
typedef struct {
  envoy_error_code_t error_code;
  envoy_data message;
} envoy_error;

#ifdef __cplusplus
extern "C" { // function pointers
#endif
/**
 * Called when all headers get received on the async HTTP stream.
 * @param headers, the headers received.
 * @param end_stream, whether the response is headers-only.
 * @param context, contains the necessary state to carry out platform-specific dispatch and
 * execution.
 */
typedef void (*envoy_on_headers_f)(envoy_headers headers, bool end_stream, void* context);
/**
 * Called when a data frame gets received on the async HTTP stream.
 * This callback can be invoked multiple times if the data gets streamed.
 * @param data, the data received.
 * @param end_stream, whether the data is the last data frame.
 * @param context, contains the necessary state to carry out platform-specific dispatch and
 * execution.
 */
typedef void (*envoy_on_data_f)(envoy_data data, bool end_stream, void* context);
/**
 * Called when a metadata frame gets received on the async HTTP stream.
 * Note that metadata frames are prohibited from ending a stream.
 * @param metadata, the metadata received.
 * @param context, contains the necessary state to carry out platform-specific dispatch and
 * execution.
 */
typedef void (*envoy_on_metadata_f)(envoy_headers metadata, void* context);
/**
 * Called when all trailers get received on the async HTTP stream.
 * Note that end stream is implied when on_trailers is called.
 * @param trailers, the trailers received.
 * @param context, contains the necessary state to carry out platform-specific dispatch and
 * execution.
 */
typedef void (*envoy_on_trailers_f)(envoy_headers trailers, void* context);
/**
 * Called when the async HTTP stream has an error.
 * @param envoy_error, the error received/caused by the async HTTP stream.
 * @param context, contains the necessary state to carry out platform-specific dispatch and
 * execution.
 */
typedef void (*envoy_on_error_f)(envoy_error error, void* context);

/**
 * Called when the async HTTP stream has completed without an error bi-directionally.
 * @param context, contains the necessary state to carry out platform-specific dispatch and
 * execution.
 */
typedef void (*envoy_on_complete_f)(void* context);

#ifdef __cplusplus
} // function pointers
#endif

/**
 * Interface that can handle HTTP callbacks.
 */
typedef struct {
  envoy_on_headers_f on_headers;
  envoy_on_data_f on_data;
  envoy_on_metadata_f on_metadata;
  envoy_on_trailers_f on_trailers;
  envoy_on_error_f on_error;
  envoy_on_complete_f on_complete;
  void* context; // Will be passed through to callbacks to provide dispatch and execution state.
} envoy_http_callbacks;
