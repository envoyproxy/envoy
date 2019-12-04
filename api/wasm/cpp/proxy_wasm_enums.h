/*
 * Intrinsic enumerations available to WASM modules.
 */
// NOLINT(namespace-envoy)

#pragma once

enum class LogLevel : int32_t { trace, debug, info, warn, error, critical };
enum class MetricType : int32_t {
  Counter = 0,
  Gauge = 1,
  Histogram = 2,
};
