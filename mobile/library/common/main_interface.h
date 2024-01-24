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
 * Update the network interface to the preferred network for opening new streams.
 * Note that this state is shared by all engines.
 * @param engine, the engine whose preferred network should be set.
 * @param network, the network to be preferred for new streams.
 * @return envoy_status_t, the resulting status of the operation.
 */
envoy_status_t set_preferred_network(envoy_engine_t engine, envoy_network_t network);

/**
 * @brief Update the currently active proxy settings.
 *
 * @param engine, the engine whose proxy settings should be updated.
 * @param host, the proxy host.
 * @param port, the proxy port.
 * @return envoy_status_t, the resulting status of the operation.
 */
envoy_status_t set_proxy_settings(envoy_engine_t engine, const char* host, const uint16_t port);

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
 * Collect a snapshot of all active stats.
 * Note: this function may block for some time while collecting stats.
 * @param engine, the engine whose stats to dump.
 * @param data, out parameter to populate with stats data.
 */
envoy_status_t dump_stats(envoy_engine_t engine, envoy_data* data);

/**
 * Statically register APIs leveraging platform libraries.
 * Warning: Must be completed before any calls to run_engine().
 * @param name, identifier of the platform API
 * @param api, type-erased c struct containing function pointers and context.
 * @return envoy_status_t, the resulting status of the operation.
 */
envoy_status_t register_platform_api(const char* name, void* api);

/**
 * Refresh DNS, and drain connections associated with an engine.
 * @param engine, handle to the engine to drain.
 * @return envoy_status_t, the resulting status of the operation.
 */
envoy_status_t reset_connectivity_state(envoy_engine_t engine);

#ifdef __cplusplus
} // functions
#endif
