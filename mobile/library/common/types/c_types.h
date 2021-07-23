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

/**
 * Networks classified by last physical link.
 * ENVOY_NET_GENERIC is default and includes cases where network characteristics are unknown.
 * ENVOY_NET_WLAN includes WiFi and other local area wireless networks.
 * ENVOY_NET_WWAN includes all mobile phone networks.
 */
typedef enum {
  ENVOY_NET_GENERIC = 0,
  ENVOY_NET_WLAN = 1,
  ENVOY_NET_WWAN = 2,
} envoy_network_t;

// The name used to registered event tracker api.
extern const char* envoy_event_tracker_api_name;

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

#ifdef __cplusplus
extern "C" { // function pointers
#endif

/**
 * Callback signature for headers on an HTTP stream.
 *
 * @param headers, the headers received.
 * @param end_stream, whether the response is headers-only.
 * @param context, contains the necessary state to carry out platform-specific dispatch and
 * execution.
 * @return void*, return context (may be unused).
 */
typedef void* (*envoy_on_headers_f)(envoy_headers headers, bool end_stream, void* context);

/**
 * Callback signature for data on an HTTP stream.
 *
 * This callback can be invoked multiple times when data is streamed.
 *
 * @param data, the data received.
 * @param end_stream, whether the data is the last data frame.
 * @param context, contains the necessary state to carry out platform-specific dispatch and
 * execution.
 * @return void*, return context (may be unused).
 */
typedef void* (*envoy_on_data_f)(envoy_data data, bool end_stream, void* context);

/**
 * Callback signature for metadata on an HTTP stream.
 *
 * Note that metadata frames are prohibited from ending a stream.
 *
 * @param metadata, the metadata received.
 * @param context, contains the necessary state to carry out platform-specific dispatch and
 * execution.
 * @return void*, return context (may be unused).
 */
typedef void* (*envoy_on_metadata_f)(envoy_headers metadata, void* context);

/**
 * Callback signature for trailers on an HTTP stream.
 *
 * Note that end stream is implied when on_trailers is called.
 *
 * @param trailers, the trailers received.
 * @param context, contains the necessary state to carry out platform-specific dispatch and
 * execution.
 * @return void*, return context (may be unused).
 */
typedef void* (*envoy_on_trailers_f)(envoy_headers trailers, void* context);

/**
 * Callback signature for errors with an HTTP stream.
 *
 * This is a TERMINAL callback. Exactly one terminal callback will be called per stream.
 *
 * @param envoy_error, the error received/caused by the async HTTP stream.
 * @param context, contains the necessary state to carry out platform-specific dispatch and
 * execution.
 * @return void*, return context (may be unused).
 */
typedef void* (*envoy_on_error_f)(envoy_error error, void* context);

/**
 * Callback signature for when an HTTP stream bi-directionally completes without error.
 *
 * This is a TERMINAL callback. Exactly one terminal callback will be called per stream.
 *
 * @param context, contains the necessary state to carry out platform-specific dispatch and
 * execution.
 * @return void*, return context (may be unused).
 */
typedef void* (*envoy_on_complete_f)(void* context);

/**
 * Callback signature for when an HTTP stream is cancelled.
 *
 * This is a TERMINAL callback. Exactly one terminal callback will be called per stream.
 *
 * @param context, contains the necessary state to carry out platform-specific dispatch and
 * execution.
 * @return void*, return context (may be unused).
 */
typedef void* (*envoy_on_cancel_f)(void* context);

/**
 * Called when the envoy engine is exiting.
 */
typedef void (*envoy_on_exit_f)(void* context);

/**
 * Called when the envoy has finished its async setup and returned post-init callbacks.
 *
 * @param context, contains the necessary state to carry out platform-specific dispatch and
 * execution.
 */
typedef void (*envoy_on_engine_running_f)(void* context);

/**
 * Called when envoy's logger logs data.
 *
 * @param data, the logged data.
 * @param context, contains the necessary state to carry out platform-specific dispatch and
 * execution.
 */
typedef void (*envoy_logger_log_f)(envoy_data data, const void* context);

/**
 * Called when Envoy is done with the logger.
 *
 * @param context, contains the necessary state to carry out platform-specific dispatch and
 * execution.
 */
typedef void (*envoy_logger_release_f)(const void* context);

/**
 * Called when envoy's event tracker tracks an event.
 *
 * @param event, the dictionary with attributes that describe the event.
 * @param context, contains the necessary state to carry out platform-specific dispatch and
 * execution.
 */
typedef void (*envoy_event_tracker_track_f)(envoy_map event, const void* context);

#ifdef __cplusplus
} // function pointers
#endif

/**
 * Interface to handle HTTP callbacks.
 */
typedef struct {
  envoy_on_headers_f on_headers;
  envoy_on_data_f on_data;
  envoy_on_metadata_f on_metadata;
  envoy_on_trailers_f on_trailers;
  envoy_on_error_f on_error;
  envoy_on_complete_f on_complete;
  envoy_on_cancel_f on_cancel;
  // Context passed through to callbacks to provide dispatch and execution state.
  void* context;
} envoy_http_callbacks;

/**
 * Interface that can handle engine callbacks.
 */
typedef struct {
  envoy_on_engine_running_f on_engine_running;
  envoy_on_exit_f on_exit;
  // Context passed through to callbacks to provide dispatch and execution state.
  void* context;
} envoy_engine_callbacks;

/**
 * Interface for logging.
 */
typedef struct {
  envoy_logger_log_f log;
  envoy_logger_release_f release;
  // Context passed through to callbacks to provide dispatch and execution state.
  const void* context;
} envoy_logger;

/**
 * Interface for event tracking.
 */
typedef struct {
  envoy_event_tracker_track_f track;
  // Context passed through to callbacks to provide dispatch and execution state.
  const void* context;
} envoy_event_tracker;
