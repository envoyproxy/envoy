#pragma once

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
  // Responsible for converting groups of metrics into the raw output format (such as prometheus
  // text exposition format or prometheus protobuf exposition format).
  class OutputFormat {
  public:
    virtual ~OutputFormat() = default;

    void setHistogramsAsSummaries(bool histograms_as_summaries) {
      histograms_as_summaries_ = histograms_as_summaries;
    }

    bool histogramsAsSummaries() const { return histograms_as_summaries_; }

    // Return the prometheus output for a group of Counters.
    virtual void generateOutput(Buffer::Instance& output,
                                const std::vector<const Stats::Counter*>& counters,
                                const std::string& prefixed_tag_extracted_name) const PURE;

    // Return the prometheus output for a group of PrimitiveCounters.
    virtual void generateOutput(Buffer::Instance& output,
                                const std::vector<const Stats::PrimitiveCounterSnapshot*>& counters,
                                const std::string& prefixed_tag_extracted_name) const PURE;

    // Return the prometheus output for a group of Gauges.
    virtual void generateOutput(Buffer::Instance& output,
                                const std::vector<const Stats::Gauge*>& gauges,
                                const std::string& prefixed_tag_extracted_name) const PURE;

    // Returns the prometheus output for a group of TextReadouts.
    virtual void generateOutput(Buffer::Instance& output,
                                const std::vector<const Stats::TextReadout*>& text_readouts,
                                const std::string& prefixed_tag_extracted_name) const PURE;

    // Return the prometheus output for a group of PrimitiveGauges.
    virtual void generateOutput(Buffer::Instance& output,
                                const std::vector<const Stats::PrimitiveGaugeSnapshot*>& gauges,
                                const std::string& prefixed_tag_extracted_name) const PURE;

    // Return the prometheus output for a group of Histograms.
    virtual void generateOutput(Buffer::Instance& output,
                                const std::vector<const Stats::ParentHistogram*>& histograms,
                                const std::string& prefixed_tag_extracted_name) const PURE;

  private:
    bool histograms_as_summaries_{false};
  };

  /**
   * Extracts counters and gauges and relevant tags, appending them to
   * the response buffer after sanitizing the metric / label names.
   * Detects based on request headers whether to emit text or protobuf format.
   * @return uint64_t total number of metric types inserted in response.
   */
  static uint64_t statsAsPrometheus(const std::vector<Stats::CounterSharedPtr>& counters,
                                    const std::vector<Stats::GaugeSharedPtr>& gauges,
                                    const std::vector<Stats::ParentHistogramSharedPtr>& histograms,
                                    const std::vector<Stats::TextReadoutSharedPtr>& text_readouts,
                                    const Upstream::ClusterManager& cluster_manager,
                                    const Http::RequestHeaderMap& request_headers,
                                    Http::ResponseHeaderMap& response_headers,
                                    Buffer::Instance& response, const StatsParams& params,
                                    const Stats::CustomStatNamespaces& custom_namespaces);

  static uint64_t
  statsAsPrometheusText(const std::vector<Stats::CounterSharedPtr>& counters,
                        const std::vector<Stats::GaugeSharedPtr>& gauges,
                        const std::vector<Stats::ParentHistogramSharedPtr>& histograms,
                        const std::vector<Stats::TextReadoutSharedPtr>& text_readouts,
                        const Upstream::ClusterManager& cluster_manager, Buffer::Instance& response,
                        const StatsParams& params,
                        const Stats::CustomStatNamespaces& custom_namespaces);

  static uint64_t
  statsAsPrometheusProtobuf(const std::vector<Stats::CounterSharedPtr>& counters,
                            const std::vector<Stats::GaugeSharedPtr>& gauges,
                            const std::vector<Stats::ParentHistogramSharedPtr>& histograms,
                            const std::vector<Stats::TextReadoutSharedPtr>& text_readouts,
                            const Upstream::ClusterManager& cluster_manager,
                            Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
                            const StatsParams& params,
                            const Stats::CustomStatNamespaces& custom_namespaces);

  static uint64_t
  generateWithOutputFormat(const std::vector<Stats::CounterSharedPtr>& counters,
                           const std::vector<Stats::GaugeSharedPtr>& gauges,
                           const std::vector<Stats::ParentHistogramSharedPtr>& histograms,
                           const std::vector<Stats::TextReadoutSharedPtr>& text_readouts,
                           const Upstream::ClusterManager& cluster_manager,
                           Buffer::Instance& response, const StatsParams& params,
                           const Stats::CustomStatNamespaces& custom_namespaces,
                           OutputFormat& output_format);

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
