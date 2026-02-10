#include "source/server/admin/prometheus_stats.h"

#include <cmath>

#include "source/common/common/empty_string.h"
#include "source/common/common/macros.h"
#include "source/common/common/regex.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/stats/histogram_impl.h"
#include "source/common/upstream/host_utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "io/prometheus/client/metrics.pb.h"

namespace Envoy {
namespace Server {

namespace {

const Regex::CompiledGoogleReMatcher& promRegex() {
  CONSTRUCT_ON_FIRST_USE(Regex::CompiledGoogleReMatcherNoSafetyChecks, "[^a-zA-Z0-9_]");
}

/**
 * Take a string and sanitize it according to Prometheus conventions.
 */
std::string sanitizeName(const absl::string_view name) {
  // The name must match the regex [a-zA-Z_][a-zA-Z0-9_]* as required by
  // prometheus. Refer to https://prometheus.io/docs/concepts/data_model/.
  // The initial [a-zA-Z_] constraint is always satisfied by the namespace prefix.
  return promRegex().replaceAll(name, "_");
}

/**
 * Take tag values and sanitize it for text serialization, according to
 * Prometheus conventions.
 */
std::string sanitizeValue(const absl::string_view value) {
  // Removes problematic characters from Prometheus tag values to prevent
  // text serialization issues. This matches the prometheus text formatting code:
  // https://github.com/prometheus/common/blob/88f1636b699ae4fb949d292ffb904c205bf542c9/expfmt/text_create.go#L419-L420.
  // The goal is to replace '\' with "\\", newline with "\n", and '"' with "\"".
  return absl::StrReplaceAll(value, {
                                        {R"(\)", R"(\\)"},
                                        {"\n", R"(\n)"},
                                        {R"(")", R"(\")"},
                                    });
}

/*
 * Comparator for Stats::Metric that does not require a string representation
 * to make the comparison, for memory efficiency.
 */
struct MetricLessThan {
  bool operator()(const Stats::Metric* a, const Stats::Metric* b) const {
    ASSERT(&a->constSymbolTable() == &b->constSymbolTable());
    return a->constSymbolTable().lessThan(a->statName(), b->statName());
  }
};

struct PrimitiveMetricSnapshotLessThan {
  bool operator()(const Stats::PrimitiveMetricMetadata* a,
                  const Stats::PrimitiveMetricMetadata* b) {
    return a->name() < b->name();
  }
};

class TextFormat : public PrometheusStatsFormatter::OutputFormat {
public:
  void generateOutput(Buffer::Instance& output, const std::vector<const Stats::Counter*>& counters,
                      const std::string& prefixed_tag_extracted_name) const override {
    generateNumericOutput(output, counters, prefixed_tag_extracted_name);
  }

  void generateOutput(Buffer::Instance& output,
                      const std::vector<const Stats::PrimitiveCounterSnapshot*>& counters,
                      const std::string& prefixed_tag_extracted_name) const override {
    generateNumericOutput(output, counters, prefixed_tag_extracted_name);
  }

  void generateOutput(Buffer::Instance& output, const std::vector<const Stats::Gauge*>& gauges,
                      const std::string& prefixed_tag_extracted_name) const override {
    generateNumericOutput(output, gauges, prefixed_tag_extracted_name);
  }

  void generateOutput(Buffer::Instance& output,
                      const std::vector<const Stats::PrimitiveGaugeSnapshot*>& gauges,
                      const std::string& prefixed_tag_extracted_name) const override {
    generateNumericOutput(output, gauges, prefixed_tag_extracted_name);
  }

  void generateOutput(Buffer::Instance& output,
                      const std::vector<const Stats::ParentHistogram*>& histograms,
                      const std::string& prefixed_tag_extracted_name) const override {
    if (histogramsAsSummaries()) {
      generateSummaryOutput(output, histograms, prefixed_tag_extracted_name);
    } else {
      generateHistogramOutput(output, histograms, prefixed_tag_extracted_name);
    }
  }

  /*
   * Returns the prometheus output for a group of TextReadouts in gauge format.
   * It is a workaround of a limitation of prometheus which stores only numeric metrics.
   * The output is a gauge named the same as a given text-readout. The value of returned gauge is
   * always equal to 0. Returned gauge contains all tags of a given text-readout and one additional
   * tag {"text_value":"textReadout.value"}.
   */
  void generateOutput(Buffer::Instance& output,
                      const std::vector<const Stats::TextReadout*>& text_readouts,
                      const std::string& prefixed_tag_extracted_name) const override {
    // TextReadout stats are returned in gauge format, so "gauge" type is set intentionally.
    generateTypeOutput(output, "gauge", prefixed_tag_extracted_name);

    for (const auto* text_readout : text_readouts) {
      auto tags = text_readout->tags();
      tags.push_back(Stats::Tag{"text_value", text_readout->value()});
      const std::string formattedTags = PrometheusStatsFormatter::formattedTags(tags);
      output.add(fmt::format("{0}{{{1}}} 0\n", prefixed_tag_extracted_name, formattedTags));
    }
  }

private:
  void generateTypeOutput(Buffer::Instance& output, absl::string_view type,
                          const std::string& prefixed_tag_extracted_name) const {
    output.add(fmt::format("# TYPE {0} {1}\n", prefixed_tag_extracted_name, type));
  }

  template <class StatType>
  void generateNumericOutput(Buffer::Instance& output, const std::vector<const StatType*>& metrics,
                             const std::string& prefixed_tag_extracted_name) const {
    absl::string_view type;
    if constexpr (std::is_same_v<Stats::Counter, StatType> ||
                  std::is_same_v<Stats::PrimitiveCounterSnapshot, StatType>) {
      type = "counter";
    } else if constexpr (std::is_same_v<Stats::Gauge, StatType> ||
                         std::is_same_v<Stats::PrimitiveGaugeSnapshot, StatType>) {
      type = "gauge";
    } else {
      static_assert(false, "Unexpected StatsType");
    }

    generateTypeOutput(output, type, prefixed_tag_extracted_name);
    for (const auto* metric : metrics) {
      const std::string formatted_tags = PrometheusStatsFormatter::formattedTags(metric->tags());
      output.add(fmt::format("{0}{{{1}}} {2}\n", prefixed_tag_extracted_name, formatted_tags,
                             metric->value()));
    }
  }

  /*
   * Returns the prometheus output for a histogram. The output is a multi-line string (with embedded
   * newlines) that contains all the individual bucket counts and sum/count for a single histogram
   * (metric_name plus all tags).
   */
  void generateHistogramOutput(Buffer::Instance& output,
                               const std::vector<const Stats::ParentHistogram*>& histograms,
                               const std::string& prefixed_tag_extracted_name) const {
    generateTypeOutput(output, "histogram", prefixed_tag_extracted_name);

    for (const auto* histogram : histograms) {
      const std::string tags = PrometheusStatsFormatter::formattedTags(histogram->tags());
      const std::string hist_tags = histogram->tags().empty() ? EMPTY_STRING : (tags + ",");

      const Stats::HistogramStatistics& stats = histogram->cumulativeStatistics();
      Stats::ConstSupportedBuckets& supported_buckets = stats.supportedBuckets();
      const std::vector<uint64_t>& computed_buckets = stats.computedBuckets();
      for (size_t i = 0; i < supported_buckets.size(); ++i) {
        double bucket = supported_buckets[i];
        uint64_t value = computed_buckets[i];
        // We want to print the bucket in a fixed point (non-scientific) format. The fmt library
        // doesn't have a specific modifier to format as a fixed-point value only so we use the
        // 'g' operator which prints the number in general fixed point format or scientific format
        // with precision 50 to round the number up to 32 significant digits in fixed point format
        // which should cover pretty much all cases
        output.add(fmt::format("{0}_bucket{{{1}le=\"{2:.32g}\"}} {3}\n",
                               prefixed_tag_extracted_name, hist_tags, bucket, value));
      }

      output.add(fmt::format("{0}_bucket{{{1}le=\"+Inf\"}} {2}\n", prefixed_tag_extracted_name,
                             hist_tags, stats.sampleCount()));
      output.add(fmt::format("{0}_sum{{{1}}} {2:.32g}\n", prefixed_tag_extracted_name, tags,
                             stats.sampleSum()));
      output.add(fmt::format("{0}_count{{{1}}} {2}\n", prefixed_tag_extracted_name, tags,
                             stats.sampleCount()));
    }
  }

  /*
   * Returns the prometheus output for a summary. The output is a multi-line string (with embedded
   * newlines) that contains all the individual quantile values and sum/count for a single histogram
   * (metric_name plus all tags).
   */
  void generateSummaryOutput(Buffer::Instance& output,
                             const std::vector<const Stats::ParentHistogram*>& histograms,
                             const std::string& prefixed_tag_extracted_name) const {
    generateTypeOutput(output, "summary", prefixed_tag_extracted_name);

    for (const auto* histogram : histograms) {
      const std::string tags = PrometheusStatsFormatter::formattedTags(histogram->tags());
      const std::string hist_tags = histogram->tags().empty() ? EMPTY_STRING : (tags + ",");

      const Stats::HistogramStatistics& stats = histogram->intervalStatistics();
      Stats::ConstSupportedBuckets& supported_quantiles = stats.supportedQuantiles();
      const std::vector<double>& computed_quantiles = stats.computedQuantiles();
      for (size_t i = 0; i < supported_quantiles.size(); ++i) {
        double quantile = supported_quantiles[i];
        double value = computed_quantiles[i];
        output.add(fmt::format("{0}{{{1}quantile=\"{2}\"}} {3:.32g}\n", prefixed_tag_extracted_name,
                               hist_tags, quantile, value));
      }

      output.add(fmt::format("{0}_sum{{{1}}} {2:.32g}\n", prefixed_tag_extracted_name, tags,
                             stats.sampleSum()));
      output.add(fmt::format("{0}_count{{{1}}} {2}\n", prefixed_tag_extracted_name, tags,
                             stats.sampleCount()));
    }
  }
};

class ProtobufFormat : public PrometheusStatsFormatter::OutputFormat {
public:
  void generateOutput(Buffer::Instance& output, const std::vector<const Stats::Counter*>& counters,
                      const std::string& prefixed_tag_extracted_name) const override {
    generateNumericOutput(output, counters, prefixed_tag_extracted_name,
                          io::prometheus::client::MetricType::COUNTER);
  }

  // Return the prometheus output for a group of PrimitiveCounters.
  void generateOutput(Buffer::Instance& output,
                      const std::vector<const Stats::PrimitiveCounterSnapshot*>& counters,
                      const std::string& prefixed_tag_extracted_name) const override {
    generateNumericOutput(output, counters, prefixed_tag_extracted_name,
                          io::prometheus::client::MetricType::COUNTER);
  }

  // Return the prometheus output for a group of Gauges.
  void generateOutput(Buffer::Instance& output, const std::vector<const Stats::Gauge*>& gauges,
                      const std::string& prefixed_tag_extracted_name) const override {
    generateNumericOutput(output, gauges, prefixed_tag_extracted_name,
                          io::prometheus::client::MetricType::GAUGE);
  }

  // Returns the prometheus output for a group of TextReadouts.
  void generateOutput(Buffer::Instance& output,
                      const std::vector<const Stats::TextReadout*>& text_readouts,
                      const std::string& prefixed_tag_extracted_name) const override {
    ASSERT(!text_readouts.empty());

    io::prometheus::client::MetricFamily metric_family;
    metric_family.set_name(prefixed_tag_extracted_name);
    metric_family.set_type(io::prometheus::client::MetricType::GAUGE);
    metric_family.mutable_metric()->Reserve(text_readouts.size());

    for (const auto* text_readout : text_readouts) {
      auto* metric = metric_family.add_metric();
      addLabelsToMetric(metric, text_readout->tags());

      // Add text_value tag
      auto* text_label = metric->add_label();
      text_label->set_name("text_value");
      text_label->set_value(sanitizeValue(text_readout->value()));

      // Set gauge value to 0
      auto* gauge = metric->mutable_gauge();
      gauge->set_value(0);
    }

    writeDelimitedMessage(metric_family, output);
  }

  // Return the prometheus output for a group of PrimitiveGauges.
  void generateOutput(Buffer::Instance& output,
                      const std::vector<const Stats::PrimitiveGaugeSnapshot*>& gauges,
                      const std::string& prefixed_tag_extracted_name) const override {
    generateNumericOutput(output, gauges, prefixed_tag_extracted_name,
                          io::prometheus::client::MetricType::GAUGE);
  }

  // Return the prometheus output for a group of Histograms.
  void generateOutput(Buffer::Instance& output,
                      const std::vector<const Stats::ParentHistogram*>& histograms,
                      const std::string& prefixed_tag_extracted_name) const override {
    ASSERT(!histograms.empty());

    io::prometheus::client::MetricFamily metric_family;
    metric_family.set_name(prefixed_tag_extracted_name);

    if (histogramsAsSummaries()) {
      generateSummaryOutput(metric_family, histograms);
    } else {
      generateHistogramOutput(metric_family, histograms);
    }

    writeDelimitedMessage(metric_family, output);
  }

private:
  // Helper method to add labels to a metric from tags.
  void addLabelsToMetric(io::prometheus::client::Metric* metric,
                         const std::vector<Stats::Tag>& tags) const {
    metric->mutable_label()->Reserve(tags.size());
    for (const auto& tag : tags) {
      auto* label = metric->add_label();
      label->set_name(sanitizeName(tag.name_));
      label->set_value(sanitizeValue(tag.value_));
    }
  }

  template <class StatType>
  void generateNumericOutput(Buffer::Instance& output, const std::vector<const StatType*>& metrics,
                             const std::string& prefixed_tag_extracted_name,
                             io::prometheus::client::MetricType type) const {
    ASSERT(!metrics.empty());

    io::prometheus::client::MetricFamily metric_family;
    metric_family.set_name(prefixed_tag_extracted_name);
    metric_family.set_type(type);
    metric_family.mutable_metric()->Reserve(metrics.size());

    for (const auto* metric : metrics) {
      auto* prom_metric = metric_family.add_metric();
      addLabelsToMetric(prom_metric, metric->tags());

      // Set value based on type
      if (type == io::prometheus::client::MetricType::COUNTER) {
        auto* counter = prom_metric->mutable_counter();
        counter->set_value(metric->value());
      } else {
        auto* gauge = prom_metric->mutable_gauge();
        gauge->set_value(metric->value());
      }
    }

    writeDelimitedMessage(metric_family, output);
  }

  void generateHistogramOutput(io::prometheus::client::MetricFamily& metric_family,
                               const std::vector<const Stats::ParentHistogram*>& histograms) const {
    metric_family.set_type(io::prometheus::client::MetricType::HISTOGRAM);
    metric_family.mutable_metric()->Reserve(histograms.size());

    for (const auto* histogram : histograms) {
      auto* metric = metric_family.add_metric();
      addLabelsToMetric(metric, histogram->tags());

      const Stats::HistogramStatistics& stats = histogram->cumulativeStatistics();
      Stats::ConstSupportedBuckets& supported_buckets = stats.supportedBuckets();
      const std::vector<uint64_t>& computed_buckets = stats.computedBuckets();

      auto* prom_histogram = metric->mutable_histogram();
      prom_histogram->set_sample_count(stats.sampleCount());
      prom_histogram->set_sample_sum(stats.sampleSum());

      prom_histogram->mutable_bucket()->Reserve(supported_buckets.size());
      for (size_t i = 0; i < supported_buckets.size(); ++i) {
        auto* bucket = prom_histogram->add_bucket();
        bucket->set_upper_bound(supported_buckets[i]);
        bucket->set_cumulative_count(computed_buckets[i]);
      }
    }
  }

  void generateSummaryOutput(io::prometheus::client::MetricFamily& metric_family,
                             const std::vector<const Stats::ParentHistogram*>& histograms) const {
    metric_family.set_type(io::prometheus::client::MetricType::SUMMARY);
    metric_family.mutable_metric()->Reserve(histograms.size());

    for (const auto* histogram : histograms) {
      auto* metric = metric_family.add_metric();
      addLabelsToMetric(metric, histogram->tags());

      const Stats::HistogramStatistics& stats = histogram->intervalStatistics();
      Stats::ConstSupportedBuckets& supported_quantiles = stats.supportedQuantiles();
      const std::vector<double>& computed_quantiles = stats.computedQuantiles();

      auto* summary = metric->mutable_summary();
      summary->set_sample_count(stats.sampleCount());
      summary->set_sample_sum(stats.sampleSum());

      summary->mutable_quantile()->Reserve(supported_quantiles.size());
      for (size_t i = 0; i < supported_quantiles.size(); ++i) {
        auto* quantile = summary->add_quantile();
        quantile->set_quantile(supported_quantiles[i]);
        quantile->set_value(computed_quantiles[i]);
      }
    }
  }

  // Write a varint-length-delimited protobuf message to the buffer.
  void writeDelimitedMessage(const Protobuf::MessageLite& message, Buffer::Instance& output) const {
    constexpr size_t kMaxVarintLength = 10; // This is documented, but not exported as a constant.

    const size_t length = message.ByteSizeLong();
    auto reservation = output.reserveSingleSlice(length + kMaxVarintLength);
    uint8_t* const reservation_start = reinterpret_cast<uint8_t*>(reservation.slice().mem_);

    uint8_t* const end_of_varint =
        Protobuf::io::CodedOutputStream::WriteVarint64ToArray(length, reservation_start);
    message.SerializeWithCachedSizesToArray(end_of_varint);

    ASSERT(end_of_varint >= reservation_start);
    const size_t varint_size = end_of_varint - reservation_start;
    ASSERT(varint_size <= kMaxVarintLength);
    reservation.commit(varint_size + length);
  }
};

/**
 * Processes a stat type (counter, gauge, histogram) by generating all output lines, sorting
 * them by tag-extracted metric name, and then outputting them in the correct sorted order into
 * response.
 *
 * @param response The buffer to put the output into.
 * @param used_only Whether to only output stats that are used.
 * @param regex A filter on which stats to output.
 * @param metrics The metrics to output stats for. This must contain all stats of the given type
 *        to be included in the same output.
 * @param generate_output A function which returns the output text for this metric.
 * @param type The name of the prometheus metric type for used in TYPE annotations.
 */
template <class StatType>
uint64_t outputStatType(Buffer::Instance& response, const StatsParams& params,
                        const std::vector<Stats::RefcountPtr<StatType>>& metrics,
                        const PrometheusStatsFormatter::OutputFormat& output_format,
                        const Stats::CustomStatNamespaces& custom_namespaces) {

  /*
   * From
   * https://github.com/prometheus/docs/blob/master/content/docs/instrumenting/exposition_formats.md#grouping-and-sorting:
   *
   * All lines for a given metric must be provided as one single group, with the optional HELP and
   * TYPE lines first (in no particular order). Beyond that, reproducible sorting in repeated
   * expositions is preferred but not required, i.e. do not sort if the computational cost is
   * prohibitive.
   */

  // This is an unsorted collection of dumb-pointers (no need to increment then decrement every
  // refcount; ownership is held throughout by `metrics`). It is unsorted for efficiency, but will
  // be sorted before producing the final output to satisfy the "preferred" ordering from the
  // prometheus spec: metrics will be sorted by their tags' textual representation, which will be
  // consistent across calls.
  using StatTypeUnsortedCollection = std::vector<const StatType*>;

  // Return early to avoid crashing when getting the symbol table from the first metric.
  if (metrics.empty()) {
    return 0;
  }

  // There should only be one symbol table for all of the stats in the admin
  // interface. If this assumption changes, the name comparisons in this function
  // will have to change to compare to convert all StatNames to strings before
  // comparison.
  const Stats::SymbolTable& global_symbol_table = metrics.front()->constSymbolTable();

  // Sorted collection of metrics sorted by their tagExtractedName, to satisfy the requirements
  // of the exposition format.
  std::map<Stats::StatName, StatTypeUnsortedCollection, Stats::StatNameLessThan> groups(
      global_symbol_table);

  for (const auto& metric : metrics) {
    ASSERT(&global_symbol_table == &metric->constSymbolTable());
    if (!params.shouldShowMetric(*metric)) {
      continue;
    }
    groups[metric->tagExtractedStatName()].push_back(metric.get());
  }

  auto result = groups.size();
  for (auto& group : groups) {
    const absl::optional<std::string> prefixed_tag_extracted_name =
        PrometheusStatsFormatter::metricName(global_symbol_table.toString(group.first),
                                             custom_namespaces);
    if (!prefixed_tag_extracted_name.has_value()) {
      --result;
      continue;
    }

    // Sort before producing the final output to satisfy the "preferred" ordering from the
    // prometheus spec: metrics will be sorted by their tags' textual representation, which will
    // be consistent across calls.
    std::sort(group.second.begin(), group.second.end(), MetricLessThan());

    output_format.generateOutput(response, group.second, prefixed_tag_extracted_name.value());
  }
  return result;
}

template <class StatType, class OutputFormat>
uint64_t outputPrimitiveStatType(Buffer::Instance& response, const StatsParams& params,
                                 const std::vector<StatType>& metrics,
                                 const OutputFormat& output_format,
                                 const Stats::CustomStatNamespaces& custom_namespaces) {

  /*
   * From
   * https:*github.com/prometheus/docs/blob/master/content/docs/instrumenting/exposition_formats.md#grouping-and-sorting:
   *
   * All lines for a given metric must be provided as one single group, with the optional HELP and
   * TYPE lines first (in no particular order). Beyond that, reproducible sorting in repeated
   * expositions is preferred but not required, i.e. do not sort if the computational cost is
   * prohibitive.
   */

  // This is an unsorted collection of dumb-pointers (no need to increment then decrement every
  // refcount; ownership is held throughout by `metrics`). It is unsorted for efficiency, but will
  // be sorted before producing the final output to satisfy the "preferred" ordering from the
  // prometheus spec: metrics will be sorted by their tags' textual representation, which will be
  // consistent across calls.
  using StatTypeUnsortedCollection = std::vector<const StatType*>;

  // Return early to avoid crashing when getting the symbol table from the first metric.
  if (metrics.empty()) {
    return 0;
  }

  // Sorted collection of metrics sorted by their tagExtractedName, to satisfy the requirements
  // of the exposition format.
  std::map<std::string, StatTypeUnsortedCollection> groups;

  for (const auto& metric : metrics) {
    if (!params.shouldShowMetric(metric)) {
      continue;
    }
    groups[metric.tagExtractedName()].push_back(&metric);
  }

  auto result = groups.size();
  for (auto& group : groups) {
    const absl::optional<std::string> prefixed_tag_extracted_name =
        PrometheusStatsFormatter::metricName(group.first, custom_namespaces);
    if (!prefixed_tag_extracted_name.has_value()) {
      --result;
      continue;
    }

    // Sort before producing the final output to satisfy the "preferred" ordering from the
    // prometheus spec: metrics will be sorted by their tags' textual representation, which will
    // be consistent across calls.
    std::sort(group.second.begin(), group.second.end(), PrimitiveMetricSnapshotLessThan());

    output_format.generateOutput(response, group.second, prefixed_tag_extracted_name.value());
  }
  return result;
}

// Determine the format based on Accept header, using first-match priority.
// Per HTTP spec, clients SHOULD send media types in priority order.
// Text format is only selected if explicitly requested as version 0.0.4 or as fallback.
// Returns true if protobuf format should be used, false for text format.
bool useProtobufFormat(const StatsParams& params, const Http::RequestHeaderMap& headers) {
  bool use_protobuf = false; // Default to using the text format.

  if (auto prom_format = params.query_.getFirstValue("prom_protobuf"); prom_format.has_value()) {
    return true;
  }

  // Iterate through Accept headers in order and find the first supported format
  headers.get(Http::CustomHeaders::get().Accept)
      .iterate([&](const Http::HeaderEntry& accept_header) -> Http::HeaderMap::Iterate {
        absl::string_view accept_value = accept_header.value().getStringView();

        // Split by comma to handle multiple media types in one header
        std::vector<absl::string_view> media_types = absl::StrSplit(accept_value, ',');

        for (absl::string_view entry : media_types) {
          // Strip leading/trailing whitespace
          entry = absl::StripAsciiWhitespace(entry);

          // Extract the media type (before any semicolon)
          size_t semicolon_pos = entry.find(';');
          absl::string_view media_type =
              (semicolon_pos != absl::string_view::npos) ? entry.substr(0, semicolon_pos) : entry;

          if (media_type == "application/vnd.google.protobuf") {
            use_protobuf = true;
            return Http::HeaderMap::Iterate::Break;
          }

          if (media_type == "text/plain") {
            use_protobuf = false;
            return Http::HeaderMap::Iterate::Break;
          }
        }
        return Http::HeaderMap::Iterate::Continue;
      });

  // If no match found, default to text format for backward compatibility
  return use_protobuf;
}

} // namespace

std::string PrometheusStatsFormatter::formattedTags(const std::vector<Stats::Tag>& tags) {
  std::vector<std::string> buf;
  buf.reserve(tags.size());
  for (const Stats::Tag& tag : tags) {
    buf.push_back(fmt::format("{}=\"{}\"", sanitizeName(tag.name_), sanitizeValue(tag.value_)));
  }
  return absl::StrJoin(buf, ",");
}

absl::Status PrometheusStatsFormatter::validateParams(const StatsParams& params) {
  absl::Status result;
  switch (params.histogram_buckets_mode_) {
  case Utility::HistogramBucketsMode::Summary:
  case Utility::HistogramBucketsMode::Unset:
  case Utility::HistogramBucketsMode::Cumulative:
    result = absl::OkStatus();
    break;
  case Utility::HistogramBucketsMode::Detailed:
  case Utility::HistogramBucketsMode::Disjoint:
    result = absl::InvalidArgumentError("unsupported prometheus histogram bucket mode");
    break;
  }
  return result;
}

absl::optional<std::string>
PrometheusStatsFormatter::metricName(const std::string& extracted_name,
                                     const Stats::CustomStatNamespaces& custom_namespaces) {
  const absl::optional<absl::string_view> custom_namespace_stripped =
      custom_namespaces.stripRegisteredPrefix(extracted_name);
  if (custom_namespace_stripped.has_value()) {
    // This case the name has a custom namespace, and it is a custom metric.
    const std::string sanitized_name = sanitizeName(custom_namespace_stripped.value());
    // We expose these metrics without modifying (e.g. without "envoy_"),
    // so we have to check the "user-defined" stat name complies with the Prometheus naming
    // convention. Specifically the name must start with the "[a-zA-Z_]" pattern.
    // All the characters in sanitized_name are already in "[a-zA-Z0-9_]" pattern
    // thanks to sanitizeName above, so the only thing we have to do is check
    // if it does not start with digits.
    if (sanitized_name.empty() || absl::ascii_isdigit(sanitized_name.front())) {
      return absl::nullopt;
    }
    return sanitized_name;
  }

  // If it does not have a custom namespace, add namespacing prefix to avoid conflicts, as per best
  // practice: https://prometheus.io/docs/practices/naming/#metric-names Also, naming conventions on
  // https://prometheus.io/docs/concepts/data_model/
  return absl::StrCat("envoy_", sanitizeName(extracted_name));
}

uint64_t PrometheusStatsFormatter::generateWithOutputFormat(
    const std::vector<Stats::CounterSharedPtr>& counters,
    const std::vector<Stats::GaugeSharedPtr>& gauges,
    const std::vector<Stats::ParentHistogramSharedPtr>& histograms,
    const std::vector<Stats::TextReadoutSharedPtr>& text_readouts,
    const Upstream::ClusterManager& cluster_manager, Buffer::Instance& response,
    const StatsParams& params, const Stats::CustomStatNamespaces& custom_namespaces,
    OutputFormat& output_format) {

  bool histograms_as_summaries = false;

  // Validation of bucket modes is handled separately.
  switch (params.histogram_buckets_mode_) {
  case Utility::HistogramBucketsMode::Summary:
    histograms_as_summaries = true;
    break;
  case Utility::HistogramBucketsMode::Unset:
  case Utility::HistogramBucketsMode::Cumulative:
    histograms_as_summaries = false;
    break;
  // "Detailed" and "Disjoint" don't make sense for prometheus histogram semantics
  case Utility::HistogramBucketsMode::Detailed:
  case Utility::HistogramBucketsMode::Disjoint:
    IS_ENVOY_BUG("unsupported prometheus histogram bucket mode");
    break;
  }

  output_format.setHistogramsAsSummaries(histograms_as_summaries);

  uint64_t metric_name_count = 0;
  metric_name_count +=
      outputStatType<Stats::Counter>(response, params, counters, output_format, custom_namespaces);

  metric_name_count +=
      outputStatType<Stats::Gauge>(response, params, gauges, output_format, custom_namespaces);

  metric_name_count += outputStatType<Stats::TextReadout>(response, params, text_readouts,
                                                          output_format, custom_namespaces);

  metric_name_count += outputStatType<Stats::ParentHistogram>(response, params, histograms,
                                                              output_format, custom_namespaces);

  // Note: This assumes that there is no overlap in stat name between per-endpoint stats and all
  // other stats. If this is not true, then the counters/gauges for per-endpoint need to be combined
  // with the above counter/gauge calls so that stats can be properly grouped.
  std::vector<Stats::PrimitiveCounterSnapshot> host_counters;
  std::vector<Stats::PrimitiveGaugeSnapshot> host_gauges;
  Upstream::HostUtility::forEachHostMetric(
      cluster_manager,
      [&](Stats::PrimitiveCounterSnapshot&& metric) {
        host_counters.emplace_back(std::move(metric));
      },
      [&](Stats::PrimitiveGaugeSnapshot&& metric) { host_gauges.emplace_back(std::move(metric)); });

  metric_name_count +=
      outputPrimitiveStatType(response, params, host_counters, output_format, custom_namespaces);
  metric_name_count +=
      outputPrimitiveStatType(response, params, host_gauges, output_format, custom_namespaces);

  return metric_name_count;
}

uint64_t PrometheusStatsFormatter::statsAsPrometheusText(
    const std::vector<Stats::CounterSharedPtr>& counters,
    const std::vector<Stats::GaugeSharedPtr>& gauges,
    const std::vector<Stats::ParentHistogramSharedPtr>& histograms,
    const std::vector<Stats::TextReadoutSharedPtr>& text_readouts,
    const Upstream::ClusterManager& cluster_manager, Buffer::Instance& response,
    const StatsParams& params, const Stats::CustomStatNamespaces& custom_namespaces) {

  TextFormat output_format;
  return generateWithOutputFormat(counters, gauges, histograms, text_readouts, cluster_manager,
                                  response, params, custom_namespaces, output_format);
}

uint64_t PrometheusStatsFormatter::statsAsPrometheusProtobuf(
    const std::vector<Stats::CounterSharedPtr>& counters,
    const std::vector<Stats::GaugeSharedPtr>& gauges,
    const std::vector<Stats::ParentHistogramSharedPtr>& histograms,
    const std::vector<Stats::TextReadoutSharedPtr>& text_readouts,
    const Upstream::ClusterManager& cluster_manager, Http::ResponseHeaderMap& response_headers,
    Buffer::Instance& response, const StatsParams& params,
    const Stats::CustomStatNamespaces& custom_namespaces) {

  response_headers.setReferenceContentType(
      "application/vnd.google.protobuf; "
      "proto=io.prometheus.client.MetricFamily; encoding=delimited");

  ProtobufFormat output_format;
  return generateWithOutputFormat(counters, gauges, histograms, text_readouts, cluster_manager,
                                  response, params, custom_namespaces, output_format);
}

uint64_t PrometheusStatsFormatter::statsAsPrometheus(
    const std::vector<Stats::CounterSharedPtr>& counters,
    const std::vector<Stats::GaugeSharedPtr>& gauges,
    const std::vector<Stats::ParentHistogramSharedPtr>& histograms,
    const std::vector<Stats::TextReadoutSharedPtr>& text_readouts,
    const Upstream::ClusterManager& cluster_manager, const Http::RequestHeaderMap& request_headers,
    Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
    const StatsParams& params, const Stats::CustomStatNamespaces& custom_namespaces) {

  return useProtobufFormat(params, request_headers)
             ? statsAsPrometheusProtobuf(counters, gauges, histograms, text_readouts,
                                         cluster_manager, response_headers, response, params,
                                         custom_namespaces)
             : statsAsPrometheusText(counters, gauges, histograms, text_readouts, cluster_manager,
                                     response, params, custom_namespaces);
}

} // namespace Server
} // namespace Envoy
