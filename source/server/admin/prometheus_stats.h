#pragma once

#include <memory>
#include <string>
#include <vector>

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

    enum class HistogramType {
      Summary,
      ClassicHistogram,
      NativeHistogram,
    };

    void setHistogramType(HistogramType type) { histogram_type_ = type; }

    HistogramType histogramType() const { return histogram_type_; }

    // Return the prometheus output for a group of Counters.
    virtual void generateOutput(Buffer::Instance& output,
                                const std::vector<const Stats::Counter*>& counters,
                                const std::string& prefixed_tag_extracted_name) const PURE;

    // Return the prometheus output for a group of PrimitiveCounters.
    virtual void generateOutput(Buffer::Instance& output,
                                std::vector<Stats::PrimitiveCounterSnapshot*>&& counters,
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
                                std::vector<Stats::PrimitiveGaugeSnapshot*>&& gauges,
                                const std::string& prefixed_tag_extracted_name) const PURE;

    // Return the prometheus output for a group of Histograms.
    virtual void generateOutput(Buffer::Instance& output,
                                const std::vector<const Stats::ParentHistogram*>& histograms,
                                const std::string& prefixed_tag_extracted_name) const PURE;

  private:
    HistogramType histogram_type_;
  };

  /**
   * Renders prometheus output in chunks. All metrics are grouped by tag-extracted name in the
   * constructor, and each call to nextChunk() emits whole metric families, so a family is never
   * split across chunks.
   *
   * The metric vectors, custom_namespaces and output_format must outlive this object. The
   * cluster_manager and params are only used in the constructor.
   */
  class ChunkedRenderer {
  public:
    ChunkedRenderer(const std::vector<Stats::CounterSharedPtr>& counters,
                    const std::vector<Stats::GaugeSharedPtr>& gauges,
                    const std::vector<Stats::ParentHistogramSharedPtr>& histograms,
                    const std::vector<Stats::TextReadoutSharedPtr>& text_readouts,
                    const Upstream::ClusterManager& cluster_manager, const StatsParams& params,
                    const Stats::CustomStatNamespaces& custom_namespaces,
                    OutputFormat& output_format);
    ~ChunkedRenderer();

    /**
     * Appends metric families to response until at least chunk_size bytes were added, or all
     * families were rendered.
     * @return true if more families remain to be rendered.
     */
    bool nextChunk(Buffer::Instance& response, uint64_t chunk_size);

    /**
     * @return the number of metric families rendered so far.
     */
    uint64_t metricNameCount() const { return metric_name_count_; }

    // The metric families of one stat type, in output order.
    class MetricFamilies {
    public:
      virtual ~MetricFamilies() = default;
      virtual bool done() const PURE;
      // Renders the next family. Returns 1 if it was rendered, 0 if it was skipped because its
      // name is not a valid prometheus name.
      virtual uint64_t renderNext(Buffer::Instance& response, const OutputFormat& output_format,
                                  const Stats::CustomStatNamespaces& custom_namespaces) PURE;
    };

  private:
    // Skips the stat types with no families left. Returns false when everything is rendered.
    bool advance();

    const Stats::CustomStatNamespaces& custom_namespaces_;
    const OutputFormat& output_format_;
    std::vector<std::unique_ptr<MetricFamilies>> phases_;
    size_t phase_index_{0};
    uint64_t metric_name_count_{0};
  };

  /**
   * Creates the output format (text or protobuf) for the request, and sets the response content
   * type for protobuf.
   */
  static std::unique_ptr<OutputFormat>
  makeOutputFormat(const StatsParams& params, const Http::RequestHeaderMap& request_headers,
                   Http::ResponseHeaderMap& response_headers);

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
  static std::string formattedTags(std::vector<Stats::Tag>&& tags);

  /**
   * Validate the given params, returning an error on invalid arguments
   */
  static absl::Status validateParams(const StatsParams& params,
                                     const Http::RequestHeaderMap& headers);

  /**
   * Format the given metric name, and prefixed with "envoy_" if it does not have a custom
   * stat namespace. If it has a custom stat namespace AND the name without the custom namespace
   * has a valid prometheus namespace, the trimmed name is returned.
   * Otherwise, return nullopt.
   */
  static std::optional<std::string>
  metricName(std::string&& extracted_name,
             const Stats::CustomStatNamespaces& custom_namespace_factory);
};

} // namespace Server
} // namespace Envoy
