#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

// NOLINT(namespace-envoy)

/**
 * Throughout this file one may note that most callbacks take a void* context parameter, and most
 * callback structs have a void* context field. In typical practice, the value for context on the
 * struct is the one passed through in every call made to a callback. This allows platform
 * callbacks to propagate state when supplying the callbacks and later, receiving them. Common code
 * will not attempt to use or modify this state - it's purely for the platform implementation to
 * leverage. Often that might mean it contains references to platform-native objects and/or thread
 * dispatch mechanisms that can be used to dispatch the callback as appropriate to platform code.
 */

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
typedef enum {
  ENVOY_SUCCESS = 0,
  ENVOY_FAILURE = 1,
} envoy_status_t;

typedef enum {
  UNSPECIFIED = 0, // Measured quantity does not require a unit, e.g. "items".
  BYTES = 1,
  MICROSECONDS = 2,
  MILLISECONDS = 3,
} envoy_histogram_stat_unit_t;

/**
 * Equivalent constants to envoy_status_t, for contexts where the enum may not be usable.
 */
extern const int kEnvoySuccess;
extern const int kEnvoyFailure;

/**
 * Error code associated with terminal status of a HTTP stream.
 */
typedef enum {
  ENVOY_UNDEFINED_ERROR,
  ENVOY_STREAM_RESET,
  ENVOY_CONNECTION_FAILURE,
  ENVOY_BUFFER_LIMIT_EXCEEDED,
  ENVOY_REQUEST_TIMEOUT,
} envoy_error_code_t;

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

/**
 * Const version of no-op release callback.
 */
void envoy_noop_const_release(const void* context);

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
 * Holds a single key/value pair.
 */
typedef struct {
  envoy_data key;
  envoy_data value;
} envoy_map_entry;

/**
 * Consistent type for dealing with encodable/processable header counts.
 */
typedef int envoy_map_size_t;

/**
 * Holds a map as an array of envoy_map_entry structs.
 */
typedef struct {
  // Number of entries in the array.
  envoy_map_size_t length;
  // Array of map entries.
  envoy_map_entry* entries;
} envoy_map;

// Multiple header values for the same header key are supported via a comma-delimited string.
typedef envoy_map envoy_headers;

typedef envoy_map envoy_stats_tags;

/*
 * Error struct.
 */
typedef struct {
  envoy_error_code_t error_code;
  envoy_data message;
  // the number of times an operation was attempted before firing this error.
  // For instance this is used in envoy_on_error_f to account for the number of upstream requests
  // made in a retry series before the on error callback fired.
  // -1 is used in scenarios where it does not make sense to have an attempt count for an error.
  // This is different from 0, which intentionally conveys that the action was _not_ executed.
  int32_t attempt_count;
} envoy_error;

/**
 * Contains internal HTTP stream metrics, context, and other details which are
 * sent with most callbacks.
 *
 * Note these values may change over the lifecycle of a stream.
 */
typedef struct {
  // An internal identifier for the stream. -1 if not preset.
  int64_t stream_id;
  // An internal identifier for the connection carrying the stream. -1 if not present.
  int64_t connection_id;
  // The number of internal attempts to carry out a request/operation. 0 if not present.
  uint64_t attempt_count;
  // Number of bytes consumed by the non terminal callbacks out of the response.
  // NOTE: on terminal callbacks (on_complete, on_error_, on_cancel), this value will not be equal
  //       to envoy_final_stream_intel.received_byte_count. The latter represents the real number
  //       of bytes received before decompression. consumed_bytes_from_response omits the number
  //       number of bytes related to the Status Line, and is after decompression.
  uint64_t consumed_bytes_from_response;
} envoy_stream_intel;

/**
 * Contains internal HTTP stream metrics which sent at stream end.
 *
 * Note: for the signed fields, -1 means not present.
 */
typedef struct {
  // The time the stream started (a.k.a request started), in ms since the epoch.
  int64_t stream_start_ms;
  // The time the DNS resolution for this request started, in ms since the epoch.
  int64_t dns_start_ms;
  // The time the DNS resolution for this request completed, in ms since the epoch.
  int64_t dns_end_ms;
  // The time the upstream connection started, in ms since the epoch.
  // This may not be set if socket_reused is false.
  int64_t connect_start_ms;
  // The time the upstream connection completed, in ms since the epoch.
  // This may not be set if socket_reused is false.
  int64_t connect_end_ms;
  // The time the SSL handshake started, in ms since the epoch.
  // This may not be set if socket_reused is false.
  int64_t ssl_start_ms;
  // The time the SSL handshake completed, in ms since the epoch.
  // This may not be set if socket_reused is false.
  int64_t ssl_end_ms;
  // The time the first byte of the request was sent upstream, in ms since the epoch.
  int64_t sending_start_ms;
  // The time the last byte of the request was sent upstream, in ms since the epoch.
  int64_t sending_end_ms;
  // The time the first byte of the response was received, in ms since the epoch.
  int64_t response_start_ms;
  // The time when the stream reached a final state: Error, Cancel, Success.
  int64_t stream_end_ms;
  // TODO(fredyw): This should be a bool instead.
  // True if the upstream socket had been used previously.
  uint64_t socket_reused;
  // The number of bytes sent upstream.
  uint64_t sent_byte_count;
  // The number of bytes received from upstream.
  uint64_t received_byte_count;
  // The final response flags for the stream. See
  // https://github.com/envoyproxy/envoy/blob/main/envoy/stream_info/stream_info.h
  // for the CoreResponseFlag enum.
  uint64_t response_flags;
  // The upstream protocol, if an upstream connection was established. Field
  // entries are based off of Envoy's Http::Protocol
  // Http10 == 0
  // Http11 == 1
  // Http2 == 2
  // Http3 == 3
  int64_t upstream_protocol;
} envoy_final_stream_intel;

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
 * calloc wrapper that asserts that the returned pointer is valid. Otherwise, the program exits.
 * @param count, the number of elements to be allocated.
 * @param size, the size of elements in bytes.
 * @return void*, pointer to the allocated memory.
 */
void* safe_calloc(size_t count, size_t size);

/**
 * Called by a receiver of envoy_data to indicate memory/resources can be released.
 * @param data, envoy_data to release.
 */
void release_envoy_data(envoy_data data);

/**
 * Called by a receiver of envoy_map to indicate memory/resources can be released.
 * @param map, envoy_map to release.
 */
void release_envoy_map(envoy_map map);

/**
 * Called by a receiver of envoy_headers to indicate memory/resources can be released.
 * @param headers, envoy_headers to release.
 */
void release_envoy_headers(envoy_headers headers);

/**
 * Called by a receiver of envoy_stats_tags to indicate memory/resources can be released.
 * @param stats_tags, envoy_stats_tags to release.
 */
void release_envoy_stats_tags(envoy_stats_tags stats_tags);

/**
 * Called by a receiver of envoy_error to indicate memory/resources can be released.
 * @param error, envoy_error to release.
 */
void release_envoy_error(envoy_error error);

/**
 * Helper function to copy envoy_headers.
 * @param src, the envoy_headers to copy from.
 * @param envoy_headers, copied headers.
 */
envoy_headers copy_envoy_headers(envoy_headers src);

/**
 * Helper function to copy envoy_data.
 * @param src, the envoy_data to copy from.
 * @return envoy_data, the envoy_data copied from the src.
 */
envoy_data copy_envoy_data(envoy_data src);

#ifdef __cplusplus
} // utility functions
#endif

// Convenience constant to pass to function calls with no data.
// For example when sending a headers-only request.
extern const envoy_data envoy_nodata;

// Convenience constant to pass to function calls with no headers.
extern const envoy_headers envoy_noheaders;

// Convenience constant to pass to function calls with no tags.
extern const envoy_stats_tags envoy_stats_notags;
