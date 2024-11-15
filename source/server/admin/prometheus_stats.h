#pragma once

#include <regex>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/stats/custom_stat_namespaces.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/stats.h"

#include "source/server/admin/stats_params.h"

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
                                    const std::vector<Stats::TextReadoutSharedPtr>& text_readouts,
                                    const Upstream::ClusterManager& cluster_manager,
                                    Buffer::Instance& response, const StatsParams& params,
                                    const Stats::CustomStatNamespaces& custom_namespaces);
  /**
   * Format the given tags, returning a string as a comma-separated list
   * of <tag_name>="<tag_value>" pairs.
   */
  static std::string formattedTags(const std::vector<Stats::Tag>& tags);

  /**
   * Validate the given params, returning an error on invalid arguments
   */
  static absl::Status validateParams(const StatsParams& params);

  /**
   * Format the given metric name, and prefixed with "envoy_" if it does not have a custom
   * stat namespace. If it has a custom stat namespace AND the name without the custom namespace
   * has a valid prometheus namespace, the trimmed name is returned.
   * Otherwise, return nullopt.
   */
  static absl::optional<std::string>
  metricName(const std::string& extracted_name,
             const Stats::CustomStatNamespaces& custom_namespace_factory);
};

} // namespace Server
} // namespace Envoy
