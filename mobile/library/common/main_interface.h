#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "library/common/types/c_types.h"

// NOLINT(namespace-envoy)

#ifdef __cplusplus
extern "C" { // functions
#endif

/**
 * Template configuration compiled with the Envoy Mobile library.
 * More information about Envoy's config can be found at:
 * https://www.envoyproxy.io/docs/envoy/latest/configuration/configuration
 */
extern const char* config_template;

/**
 * Template configuration used for dynamic creation of the platform-bridged filter chain.
 */
extern const char* platform_filter_template;

/**
 * Initialize an underlying HTTP stream.
 * @param engine, handle to the engine that will manage this stream.
 * @return envoy_stream_t, handle to the underlying stream.
 */
envoy_stream_t init_stream(envoy_engine_t);

/**
 * Open an underlying HTTP stream. Note: Streams must be started before other other interaction can
 * can occur.
 * @param stream, handle to the stream to be started.
 * @param callbacks, the callbacks that will run the stream callbacks.
 * @return envoy_stream, with a stream handle and a success status, or a failure status.
 */
envoy_status_t start_stream(envoy_stream_t, envoy_http_callbacks callbacks);

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
 * @return envoy_status_t, the resulting status of the operation.
 */
envoy_status_t send_metadata(envoy_stream_t stream, envoy_headers metadata);

/**
 * Send trailers over an open HTTP stream. This method can only be invoked once per stream.
 * Note that this method implicitly ends the stream.
 * @param stream, the stream to send trailers over.
 * @param trailers, the trailers to send.
 * @return envoy_status_t, the resulting status of the operation.
 */
envoy_status_t send_trailers(envoy_stream_t stream, envoy_headers trailers);

/**
 * Detach all callbacks from a stream and send an interrupt upstream if supported by transport.
 * @param stream, the stream to evict.
 * @return envoy_status_t, the resulting status of the operation.
 */
envoy_status_t reset_stream(envoy_stream_t stream);

/**
 * Initialize an engine for handling network streams.
 * @return envoy_engine_t, handle to the underlying engine.
 */
envoy_engine_t init_engine();

/**
 * Update the network interface to the preferred network for opening new streams.
 * Note that this state is shared by all engines.
 * @param network, the network to be preferred for new streams.
 * @return envoy_status_t, the resulting status of the operation.
 */
envoy_status_t set_preferred_network(envoy_network_t network);

/**
 * Increment a counter with the given elements and by the given count.
 */
void record_counter(const char* elements, uint64_t count);

/**
 * Statically register APIs leveraging platform libraries.
 * Warning: Must be completed before any calls to run_engine().
 * @param name, identifier of the platform API
 * @param api, type-erased c struct containing function pointers and context.
 * @return envoy_status_t, the resulting status of the operation.
 */
envoy_status_t register_platform_api(const char* name, void* api);

/**
 * External entry point for library.
 * @param engine, handle to the engine to run.
 * @param callbacks, the callbacks that will run the engine callbacks.
 * @param config, the configuration blob to run envoy with.
 * @param log_level, the logging level to run envoy with.
 * @return envoy_status_t, the resulting status of the operation.
 */
envoy_status_t run_engine(envoy_engine_t engine, envoy_engine_callbacks callbacks,
                          const char* config, const char* log_level);

void terminate_engine(envoy_engine_t engine);

#ifdef __cplusplus
} // functions
#endif
