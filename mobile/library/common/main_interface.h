#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// NOLINT(namespace-envoy)

/**
 * Handle to an Envoy engine instance. Valid only for the lifetime of the engine and not intended
 * for any external interpretation or use.
 */
typedef uint64_t envoy_engine_t;

/**
 * Handle to an outstanding Envoy HTTP stream. Valid only for the duration of the stream and not
 * intended for any external interpretation or use.
 */
typedef uint64_t envoy_stream_t;

/**
 * Result codes returned by all calls made to this interface.
 */
typedef enum { ENVOY_SUCCESS, ENVOY_FAILURE } envoy_status_t;

/**
 * Error code associated with terminal status of a HTTP stream.
 */
typedef enum {} envoy_error_code_t;

/**
 * Common abstraction for strings.
 */
typedef struct {
  uint64_t length;
  char* data;
} envoy_string;

/**
 * Holds raw binary data as an array of bytes.
 */
typedef struct {
  uint64_t length;
  uint8_t* bytes;
} envoy_data;

/**
 * Holds data about an HTTP stream.
 */
typedef struct {
  // Status of the Envoy HTTP stream. Note that the stream might have failed inline.
  // Thus the status should be checked before pursuing other operations on the stream.
  envoy_status_t status;
  // Handle to the Envoy HTTP Stream.
  envoy_stream_t stream;
} envoy_stream;

/**
 * Holds a single name/value header.
 */
typedef struct {
  envoy_string name;
  // Multiple header values for the same header name are supported via a comma-delimited string.
  envoy_string value;
} envoy_header;

/**
 * Holds an HTTP header map as an array of envoy_header structs.
 */
typedef struct {
  // Number of header elements in the array.
  uint64_t length;
  // Array of headers.
  envoy_header* headers;
} envoy_headers;

// Convenience constant to pass to function calls with no data.
// For example when sending a headers-only request.
const envoy_data envoy_nodata = {0, NULL};

/**
 * Error struct.
 */
typedef struct {
  envoy_error_code_t error_code;
  envoy_string message;
} envoy_error;

#ifdef __cplusplus
extern "C" { // function pointers
#endif
/**
 * Called when all headers get received on the async HTTP stream.
 * @param headers, the headers received.
 * @param end_stream, whether the response is headers-only.
 */
typedef void (*on_headers)(envoy_headers headers, bool end_stream);
/**
 * Called when a data frame gets received on the async HTTP stream.
 * This callback can be invoked multiple times if the data gets streamed.
 * @param data, the data received.
 * @param end_stream, whether the data is the last data frame.
 */
typedef void (*on_data)(envoy_data data, bool end_stream);
/**
 * Called when all trailers get received on the async HTTP stream.
 * Note that end stream is implied when on_trailers is called.
 * @param trailers, the trailers received.
 */
typedef void (*on_trailers)(envoy_headers headers);
/**
 * Called when the async HTTP stream has an error.
 * @return envoy_error, the error received/caused by the async HTTP stream.
 */
typedef envoy_error (*on_error)();

#ifdef __cplusplus
} // function pointers
#endif

/**
 * Interface that can handle HTTP callbacks.
 */
typedef struct {
  on_headers h;
  on_data d;
  on_trailers t;
  on_error e;
} envoy_observer;

#ifdef __cplusplus
extern "C" { // functions
#endif

/**
 * Open an underlying HTTP stream.
 * @param observer, the observer that will run the stream callbacks.
 * @return envoy_stream, with a stream handle and a success status, or a failure status.
 */
envoy_stream start_stream(envoy_observer observer);

/**
 * Send headers over an open HTTP stream. This method can be invoked once and needs to be called
 * before send_data.
 * @param stream, the stream to send headers over.
 * @param headers, the headers to send.
 * @param end_stream, supplies whether this is headers only.
 * @return envoy_status_t, the resulting status of the operation.
 */
envoy_status_t send_headers(envoy_stream_t stream, envoy_headers headers, bool end_stream);

/**
 * Send data over an open HTTP stream. This method can be invoked multiple times.
 * @param stream, the stream to send data over.
 * @param data, the data to send.
 * @param end_stream, supplies whether this is the last data in the stream.
 * @return envoy_status_t, the resulting status of the operation.
 */
envoy_status_t send_data(envoy_stream_t stream, envoy_data data, bool end_stream);

/**
 * Send metadata over an HTTP stream. This method can be invoked multiple times.
 * @param stream, the stream to send metadata over.
 * @param metadata, the metadata to send.
 * @param end_stream, supplies whether this is the last data in the stream.
 * @return envoy_status_t, the resulting status of the operation.
 */
envoy_status_t send_metadata(envoy_stream_t stream, envoy_headers metadata, bool end_stream);

/**
 * Send trailers over an open HTTP stream. This method can only be invoked once per stream.
 * Note that this method implicitly ends the stream.
 * @param stream, the stream to send trailers over.
 * @param trailers, the trailers to send.
 * @return envoy_status_t, the resulting status of the operation.
 */
envoy_status_t send_trailers(envoy_stream_t stream, envoy_headers trailers);

/**
 * Half-close an HTTP stream. The stream will be observable and may return further data
 * via the observer callbacks. However, nothing further may be sent.
 * @param stream, the stream to close.
 * @return envoy_status_t, the resulting status of the operation.
 */
envoy_status_t locally_close_stream(envoy_stream_t stream);

/**
 * Detach all observers from a stream and send an interrupt upstream if supported by transport.
 * @param stream, the stream to evict.
 * @return envoy_status_t, the resulting status of the operation.
 */
envoy_status_t interrupt_stream(envoy_stream_t stream);

/**
 * External entrypoint for library.
 * @param config, the configuration blob to run envoy with.
 * @param log_level, the logging level to run envoy with.
 */
envoy_status_t run_engine(const char* config, const char* log_level);

#ifdef __cplusplus
} // functions
#endif
