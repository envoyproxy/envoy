#pragma once

#include <vector>

#include "envoy/server/admin.h"
#include "envoy/stats/stats.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/server/admin/stats_params.h"
#include "source/server/admin/utils.h"

namespace Envoy {
namespace Server {

// Abstract base class for rendering stats, which captures logic
// that is shared across all formats (e.g. finalizing rendering of stats).
// The APIs for generating stats output vary between formats (e.g.
// JSON vs. Prometheus) and are defined in derived classes: having a base
// render class avoids code duplication while affording the flexibility to
// capture any differences in how we generate stats output.
class StatsRenderBase {
public:
  virtual ~StatsRenderBase() = default;

  // Completes rendering any buffered data.
  virtual void finalize(Buffer::Instance& response) PURE;

  // Indicates that no stats for a particular type have been found.
  virtual void noStats(Buffer::Instance&, absl::string_view) {}
};

// Abstract class for rendering ungrouped stats.
// Every method is called "generate" differing only by the data type, to
// facilitate templatized call-sites.
class StatsRender : public StatsRenderBase {
public:
  ~StatsRender() override = default;

  // Writes a fragment for a numeric value, for counters and gauges.
  virtual void generate(Buffer::Instance& response, const std::string& name, uint64_t value) PURE;

  // Writes a json fragment for a textual value, for text readouts.
  virtual void generate(Buffer::Instance& response, const std::string& name,
                        const std::string& value) PURE;

  // Writes a histogram value.
  virtual void generate(Buffer::Instance& response, const std::string& name,
                        const Stats::ParentHistogram& histogram) PURE;
};

// Implements the Render interface for simple textual representation of stats.
class StatsTextRender : public StatsRender {
public:
  explicit StatsTextRender(const StatsParams& params);

  // StatsRender
  void generate(Buffer::Instance& response, const std::string& name, uint64_t value) override;
  void generate(Buffer::Instance& response, const std::string& name,
                const std::string& value) override;
  void generate(Buffer::Instance& response, const std::string& name,
                const Stats::ParentHistogram& histogram) override;
  void finalize(Buffer::Instance&) override;

private:
  // Computes disjoint buckets as text and adds them to the response buffer.
  void addDisjointBuckets(const std::string& name, const Stats::ParentHistogram& histogram,
                          Buffer::Instance& response);

  const Utility::HistogramBucketsMode histogram_buckets_mode_;
};

// Implements the Render interface for json output.
class StatsJsonRender : public StatsRender {
public:
  StatsJsonRender(Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
                  const StatsParams& params);

  // StatsRender
  void generate(Buffer::Instance& response, const std::string& name, uint64_t value) override;
  void generate(Buffer::Instance& response, const std::string& name,
                const std::string& value) override;
  void generate(Buffer::Instance&, const std::string& name,
                const Stats::ParentHistogram& histogram) override;
  void finalize(Buffer::Instance& response) override;

private:
  // Summarizes the buckets in the specified histogram, collecting JSON objects.
  // Note, we do not flush this buffer to the network when it grows large, and
  // if this becomes an issue it should be possible to do, noting that we are
  // one or two levels nesting below the list of scalar stats due to the Envoy
  // stats json schema, where histograms are grouped together.
  void summarizeBuckets(const std::string& name, const Stats::ParentHistogram& histogram);

  // Collects the buckets from the specified histogram.
  void collectBuckets(const std::string& name, const Stats::ParentHistogram& histogram,
                      const std::vector<uint64_t>& interval_buckets,
                      const std::vector<uint64_t>& cumulative_buckets);

  ProtobufWkt::Struct histograms_obj_;
  ProtobufWkt::Struct histograms_obj_container_;
  std::unique_ptr<ProtobufWkt::ListValue> histogram_array_;
  bool found_used_histogram_{false};
  absl::string_view delim_{""};
  const Utility::HistogramBucketsMode histogram_buckets_mode_;
  std::string name_buffer_;  // Used for Json::sanitize for names.
  std::string value_buffer_; // Used for Json::sanitize for text-readout values.
};

// Implements the Render base interface for textual representation of Prometheus stats
// (see: https://prometheus.io/docs/concepts/data_model).
// The APIs for rendering Prometheus stats take as input a string (the stat's name)
// and a vector of counters, gauges etc. (each entry in the vector holds a set of labels
// and the stat's value for that set of labels). The Prometheus stat is rendered
// as per the text-based format described at
// https://prometheus.io/docs/instrumenting/exposition_formats/#text-based-format.
class PrometheusStatsRender : public StatsRenderBase {
public:
  void generate(Buffer::Instance& response, const std::string& prefixed_tag_extracted_name,
                const std::vector<Stats::HistogramSharedPtr>& histogram);

  void generate(Buffer::Instance& response, const std::string& prefixed_tag_extracted_name,
                const std::vector<Stats::GaugeSharedPtr>& gauge);

  void generate(Buffer::Instance& response, const std::string& prefixed_tag_extracted_name,
                const std::vector<Stats::CounterSharedPtr>& counter);

  void generate(Buffer::Instance& response, const std::string& prefixed_tag_extracted_name,
                const std::vector<Stats::TextReadoutSharedPtr>& text_readout);

  void finalize(Buffer::Instance&) override;

  /**
   * Format the given tags, returning a string as a comma-separated list
   * of <tag_name>="<tag_value>" pairs.
   */
  static std::string formattedTags(const std::vector<Stats::Tag>& tags);

  /**
   * Format the given metric name, and prefixed with "envoy_" if it does not have a custom
   * stat namespace. If it has a custom stat namespace AND the name without the custom namespace
   * has a valid prometheus namespace, the trimmed name is returned.
   * Otherwise, return nullopt.
   */
  static absl::optional<std::string>
  metricName(const std::string& extracted_name,
             const Stats::CustomStatNamespaces& custom_namespace_factory);

private:
  /**
   * Take a string and sanitize it according to Prometheus conventions.
   */
  static std::string sanitizeName(const absl::string_view name);

  /**
   * Take tag values and sanitize it for text serialization, according to
   * Prometheus conventions.
   */
  static std::string sanitizeValue(const absl::string_view value);

  template <class StatType>
  void outputStatType(
      Envoy::Buffer::Instance& response, const std::vector<StatType>& metrics,
      const std::string& prefixed_tag_extracted_name,
      const std::function<std::string(
          const StatType& metric, const std::string& prefixed_tag_extracted_name)>& generate_output,
      absl::string_view type);

  /*
   * Return the prometheus output for a numeric Stat (Counter or Gauge).
   */
  template <class StatType>
  static std::string generateNumericOutput(const StatType& metric,
                                           const std::string& prefixed_tag_extracted_name);

  /*
   * Returns the prometheus output for a TextReadout in gauge format.
   * It is a workaround of a limitation of prometheus which stores only numeric metrics.
   * The output is a gauge named the same as a given text-readout. The value of returned gauge is
   * always equal to 0. Returned gauge contains all tags of a given text-readout and one additional
   * tag {"text_value":"textReadout.value"}.
   */
  static std::string generateTextReadoutOutput(const Stats::TextReadoutSharedPtr& metric,
                                               const std::string& prefixed_tag_extracted_name);

  /*
   * Returns the prometheus output for a histogram. The output is a multi-line string (with embedded
   * newlines) that contains all the individual bucket counts and sum/count for a single histogram
   * (metric_name plus all tags).
   */
  static std::string generateHistogramOutput(const Stats::HistogramSharedPtr& histogram,
                                             const std::string& prefixed_tag_extracted_name);
};

} // namespace Server
} // namespace Envoy
