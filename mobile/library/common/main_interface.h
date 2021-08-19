#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "library/common/config/templates.h"
#include "library/common/types/c_types.h"

// NOLINT(namespace-envoy)

#ifdef __cplusplus
extern "C" { // functions
#endif

/**
 * Initialize an underlying HTTP stream.
 * @param engine, handle to the engine that will manage this stream.
 * @return envoy_stream_t, handle to the underlying stream.
 */
envoy_stream_t init_stream(envoy_engine_t engine);

/**
 * Open an underlying HTTP stream. Note: Streams must be started before other other interaction can
 * can occur.
 * @param stream, handle to the stream to be started.
 * @param callbacks, the callbacks that will run the stream callbacks.
 * @param explicit_flow_control, whether to enable explicit flow control on the response stream.
 * @return envoy_stream, with a stream handle and a success status, or a failure status.
 */
envoy_status_t start_stream(envoy_stream_t stream, envoy_http_callbacks callbacks,
                            bool explicit_flow_control);

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
 * Notify the stream that the caller is ready to receive more data from the response stream. Only
 * used in explicit flow control mode.
 * @param bytes_to_read, the quantity of data the caller is prepared to process.
 */
envoy_status_t read_data(envoy_stream_t stream, size_t bytes_to_read);

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
 * Update the network interface to the preferred network for opening new streams.
 * Note that this state is shared by all engines.
 * @param network, the network to be preferred for new streams.
 * @return envoy_status_t, the resulting status of the operation.
 */
envoy_status_t set_preferred_network(envoy_network_t network);

/**
 * Increment a counter with the given elements and by the given count.
 * @param engine, the engine that owns the counter.
 * @param elements, the string that identifies the counter to increment.
 * @param tags, a map of {key, value} pairs of tags.
 * @param count, the count to increment by.
 */
envoy_status_t record_counter_inc(envoy_engine_t, const char* elements, envoy_stats_tags tags,
                                  uint64_t count);

/**
 * Set a gauge of a given string of elements with the given value.
 * @param engine, the engine that owns the gauge.
 * @param elements, the string that identifies the gauge to set value with.
 * @param tags, a map of {key, value} pairs of tags.
 * @param value, the value to set to the gauge.
 */
envoy_status_t record_gauge_set(envoy_engine_t engine, const char* elements, envoy_stats_tags tags,
                                uint64_t value);

/**
 * Add the gauge with the given string of elements and by the given amount.
 * @param engine, the engine that owns the gauge.
 * @param elements, the string that identifies the gauge to add to.
 * @param tags, a map of {key, value} pairs of tags.
 * @param amount, the amount to add to the gauge.
 */
envoy_status_t record_gauge_add(envoy_engine_t engine, const char* elements, envoy_stats_tags tags,
                                uint64_t amount);

/**
 * Subtract from the gauge with the given string of elements and by the given amount.
 * @param engine, the engine that owns the gauge.
 * @param elements, the string that identifies the gauge to subtract from.
 * @param tags, a map of {key, value} pairs of tags.
 * @param amount, amount to subtract from the gauge.
 */
envoy_status_t record_gauge_sub(envoy_engine_t engine, const char* elements, envoy_stats_tags tags,
                                uint64_t amount);

/**
 * Add another recorded amount to the histogram with the given string of elements and unit
 * measurement.
 * @param engine, the engine that owns the histogram.
 * @param elements, the string that identifies the histogram to subtract from.
 * @param tags, a map of {key, value} pairs of tags.
 * @param value, amount to record as a new value for the histogram distribution.
 * @param unit_measure, the unit of measurement (e.g. milliseconds, bytes, etc.)
 */
envoy_status_t record_histogram_value(envoy_engine_t engine, const char* elements,
                                      envoy_stats_tags tags, uint64_t value,
                                      envoy_histogram_stat_unit_t unit_measure);

/**
 * Flush the stats sinks outside of a flushing interval.
 * Note: flushing before the engine has started will result in a no-op.
 * Note: stats flushing may not be synchronous.
 * Therefore, this function may return prior to flushing taking place.
 */
void flush_stats(envoy_engine_t engine);

/**
 * Statically register APIs leveraging platform libraries.
 * Warning: Must be completed before any calls to run_engine().
 * @param name, identifier of the platform API
 * @param api, type-erased c struct containing function pointers and context.
 * @return envoy_status_t, the resulting status of the operation.
 */
envoy_status_t register_platform_api(const char* name, void* api);

/**
 * Initialize an engine for handling network streams.
 * @param callbacks, the callbacks that will run the engine callbacks.
 * @param logger, optional callbacks to handle logging.
 * @param event_tracker, an event tracker for the emission of events.
 * @return envoy_engine_t, handle to the underlying engine.
 */
envoy_engine_t init_engine(envoy_engine_callbacks callbacks, envoy_logger logger,
                           envoy_event_tracker event_tracker);

/**
 * External entry point for library.
 * @param engine, handle to the engine to run.
 * @param config, the configuration blob to run envoy with.
 * @param log_level, the logging level to run envoy with.
 * @return envoy_status_t, the resulting status of the operation.
 */
envoy_status_t run_engine(envoy_engine_t engine, const char* config, const char* log_level);

/**
 * Terminate an engine. Further interactions with a terminated engine, or streams created by a
 * terminated engine is illegal.
 * @param engine, handle to the engine to terminate.
 */
void terminate_engine(envoy_engine_t engine);

#ifdef __cplusplus
} // functions
#endif
