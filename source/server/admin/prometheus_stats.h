#pragma once

#include <regex>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/stats.h"

namespace Envoy {
namespace Server {
/**
 * Formatter for metric/labels exported to Prometheus.
 *
 * See: https://prometheus.io/docs/concepts/data_model
 */
class PrometheusStatsFormatter {
public:
  /**
   * Extracts counters and gauges and relevant tags, appending them to
   * the response buffer after sanitizing the metric / label names.
   * @return uint64_t total number of metric types inserted in response.
   */
  static uint64_t statsAsPrometheus(const std::vector<Stats::CounterSharedPtr>& counters,
                                    const std::vector<Stats::GaugeSharedPtr>& gauges,
                                    const std::vector<Stats::ParentHistogramSharedPtr>& histograms,
                                    Buffer::Instance& response, const bool used_only,
                                    const absl::optional<std::regex>& regex);
  /**
   * Format the given tags, returning a string as a comma-separated list
   * of <tag_name>="<tag_value>" pairs.
   */
  static std::string formattedTags(const std::vector<Stats::Tag>& tags);

  /**
   * Format the given metric name, prefixed with "envoy_".
   */
  static std::string metricName(const std::string& extracted_name);

  /**
   * Register a prometheus namespace, stats starting with the namespace will not be
   * automatically prefixed with envoy namespace.
   * This method must be called from the main thread.
   * @returns bool if a new namespace is registered, false if the namespace is already
   *          registered or the namespace is invalid.
   */
  static bool registerPrometheusNamespace(absl::string_view prometheus_namespace);

  /**
   * Unregister a prometheus namespace registered by `registerPrometheusNamespace`
   * This method must be called from the main thread.
   * @returns bool if the Prometheus namespace is unregistered. false if the namespace
   *          wasn't registered.
   */
  static bool unregisterPrometheusNamespace(absl::string_view prometheus_namespace);
};

} // namespace Server
} // namespace Envoy
