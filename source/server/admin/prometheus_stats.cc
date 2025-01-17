#include "source/server/admin/prometheus_stats.h"

#include <cmath>

#include "source/common/common/empty_string.h"
#include "source/common/common/macros.h"
#include "source/common/common/regex.h"
#include "source/common/stats/histogram_impl.h"
#include "source/common/upstream/host_utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"

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

std::string generateNumericOutput(uint64_t value, const Stats::TagVector& tags,
                                  const std::string& prefixed_tag_extracted_name) {
  const std::string formatted_tags = PrometheusStatsFormatter::formattedTags(tags);
  return fmt::format("{0}{{{1}}} {2}\n", prefixed_tag_extracted_name, formatted_tags, value);
}

/*
 * Return the prometheus output for a numeric Stat (Counter or Gauge).
 */
template <class StatType>
std::string generateStatNumericOutput(const StatType& metric,
                                      const std::string& prefixed_tag_extracted_name) {
  return generateNumericOutput(metric.value(), metric.tags(), prefixed_tag_extracted_name);
}

/*
 * Returns the prometheus output for a TextReadout in gauge format.
 * It is a workaround of a limitation of prometheus which stores only numeric metrics.
 * The output is a gauge named the same as a given text-readout. The value of returned gauge is
 * always equal to 0. Returned gauge contains all tags of a given text-readout and one additional
 * tag {"text_value":"textReadout.value"}.
 */
std::string generateTextReadoutOutput(const Stats::TextReadout& text_readout,
                                      const std::string& prefixed_tag_extracted_name) {
  auto tags = text_readout.tags();
  tags.push_back(Stats::Tag{"text_value", text_readout.value()});
  const std::string formattedTags = PrometheusStatsFormatter::formattedTags(tags);
  return fmt::format("{0}{{{1}}} 0\n", prefixed_tag_extracted_name, formattedTags);
}

/*
 * Returns the prometheus output for a histogram. The output is a multi-line string (with embedded
 * newlines) that contains all the individual bucket counts and sum/count for a single histogram
 * (metric_name plus all tags).
 */
std::string generateHistogramOutput(const Stats::ParentHistogram& histogram,
                                    const std::string& prefixed_tag_extracted_name) {
  const std::string tags = PrometheusStatsFormatter::formattedTags(histogram.tags());
  const std::string hist_tags = histogram.tags().empty() ? EMPTY_STRING : (tags + ",");

  const Stats::HistogramStatistics& stats = histogram.cumulativeStatistics();
  Stats::ConstSupportedBuckets& supported_buckets = stats.supportedBuckets();
  const std::vector<uint64_t>& computed_buckets = stats.computedBuckets();
  std::string output;
  for (size_t i = 0; i < supported_buckets.size(); ++i) {
    double bucket = supported_buckets[i];
    uint64_t value = computed_buckets[i];
    // We want to print the bucket in a fixed point (non-scientific) format. The fmt library
    // doesn't have a specific modifier to format as a fixed-point value only so we use the
    // 'g' operator which prints the number in general fixed point format or scientific format
    // with precision 50 to round the number up to 32 significant digits in fixed point format
    // which should cover pretty much all cases
    output.append(fmt::format("{0}_bucket{{{1}le=\"{2:.32g}\"}} {3}\n", prefixed_tag_extracted_name,
                              hist_tags, bucket, value));
  }

  output.append(fmt::format("{0}_bucket{{{1}le=\"+Inf\"}} {2}\n", prefixed_tag_extracted_name,
                            hist_tags, stats.sampleCount()));
  output.append(fmt::format("{0}_sum{{{1}}} {2:.32g}\n", prefixed_tag_extracted_name, tags,
                            stats.sampleSum()));
  output.append(fmt::format("{0}_count{{{1}}} {2}\n", prefixed_tag_extracted_name, tags,
                            stats.sampleCount()));

  return output;
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
uint64_t outputStatType(
    Buffer::Instance& response, const StatsParams& params,
    const std::vector<Stats::RefcountPtr<StatType>>& metrics,
    const std::function<std::string(
        const StatType& metric, const std::string& prefixed_tag_extracted_name)>& generate_output,
    absl::string_view type, const Stats::CustomStatNamespaces& custom_namespaces) {

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
    response.add(fmt::format("# TYPE {0} {1}\n", prefixed_tag_extracted_name.value(), type));

    // Sort before producing the final output to satisfy the "preferred" ordering from the
    // prometheus spec: metrics will be sorted by their tags' textual representation, which will
    // be consistent across calls.
    std::sort(group.second.begin(), group.second.end(), MetricLessThan());

    for (const auto& metric : group.second) {
      response.add(generate_output(*metric, prefixed_tag_extracted_name.value()));
    }
  }
  return result;
}

template <class StatType>
uint64_t outputPrimitiveStatType(Buffer::Instance& response, const StatsParams& params,
                                 const std::vector<StatType>& metrics, absl::string_view type,
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
    response.add(fmt::format("# TYPE {0} {1}\n", prefixed_tag_extracted_name.value(), type));

    // Sort before producing the final output to satisfy the "preferred" ordering from the
    // prometheus spec: metrics will be sorted by their tags' textual representation, which will
    // be consistent across calls.
    std::sort(group.second.begin(), group.second.end(), PrimitiveMetricSnapshotLessThan());

    for (const auto& metric : group.second) {
      response.add(generateNumericOutput(metric->value(), metric->tags(),
                                         prefixed_tag_extracted_name.value()));
    }
  }
  return result;
}

/*
 * Returns the prometheus output for a summary. The output is a multi-line string (with embedded
 * newlines) that contains all the individual quantile values and sum/count for a single histogram
 * (metric_name plus all tags).
 */
std::string generateSummaryOutput(const Stats::ParentHistogram& histogram,
                                  const std::string& prefixed_tag_extracted_name) {
  const std::string tags = PrometheusStatsFormatter::formattedTags(histogram.tags());
  const std::string hist_tags = histogram.tags().empty() ? EMPTY_STRING : (tags + ",");

  const Stats::HistogramStatistics& stats = histogram.intervalStatistics();
  Stats::ConstSupportedBuckets& supported_quantiles = stats.supportedQuantiles();
  const std::vector<double>& computed_quantiles = stats.computedQuantiles();
  std::string output;
  for (size_t i = 0; i < supported_quantiles.size(); ++i) {
    double quantile = supported_quantiles[i];
    double value = computed_quantiles[i];
    output.append(fmt::format("{0}{{{1}quantile=\"{2}\"}} {3:.32g}\n", prefixed_tag_extracted_name,
                              hist_tags, quantile, value));
  }

  output.append(fmt::format("{0}_sum{{{1}}} {2:.32g}\n", prefixed_tag_extracted_name, tags,
                            stats.sampleSum()));
  output.append(fmt::format("{0}_count{{{1}}} {2}\n", prefixed_tag_extracted_name, tags,
                            stats.sampleCount()));

  return output;
};

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

uint64_t PrometheusStatsFormatter::statsAsPrometheus(
    const std::vector<Stats::CounterSharedPtr>& counters,
    const std::vector<Stats::GaugeSharedPtr>& gauges,
    const std::vector<Stats::ParentHistogramSharedPtr>& histograms,
    const std::vector<Stats::TextReadoutSharedPtr>& text_readouts,
    const Upstream::ClusterManager& cluster_manager, Buffer::Instance& response,
    const StatsParams& params, const Stats::CustomStatNamespaces& custom_namespaces) {

  uint64_t metric_name_count = 0;
  metric_name_count += outputStatType<Stats::Counter>(response, params, counters,
                                                      generateStatNumericOutput<Stats::Counter>,
                                                      "counter", custom_namespaces);

  metric_name_count += outputStatType<Stats::Gauge>(response, params, gauges,
                                                    generateStatNumericOutput<Stats::Gauge>,
                                                    "gauge", custom_namespaces);

  // TextReadout stats are returned in gauge format, so "gauge" type is set intentionally.
  metric_name_count += outputStatType<Stats::TextReadout>(
      response, params, text_readouts, generateTextReadoutOutput, "gauge", custom_namespaces);

  // validation of bucket modes is handled separately
  switch (params.histogram_buckets_mode_) {
  case Utility::HistogramBucketsMode::Summary:
    metric_name_count += outputStatType<Stats::ParentHistogram>(
        response, params, histograms, generateSummaryOutput, "summary", custom_namespaces);
    break;
  case Utility::HistogramBucketsMode::Unset:
  case Utility::HistogramBucketsMode::Cumulative:
    metric_name_count += outputStatType<Stats::ParentHistogram>(
        response, params, histograms, generateHistogramOutput, "histogram", custom_namespaces);
    break;
  // "Detailed" and "Disjoint" don't make sense for prometheus histogram semantics
  case Utility::HistogramBucketsMode::Detailed:
  case Utility::HistogramBucketsMode::Disjoint:
    IS_ENVOY_BUG("unsupported prometheus histogram bucket mode");
    break;
  }

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
      outputPrimitiveStatType(response, params, host_counters, "counter", custom_namespaces);

  metric_name_count +=
      outputPrimitiveStatType(response, params, host_gauges, "gauge", custom_namespaces);

  return metric_name_count;
}

} // namespace Server
} // namespace Envoy
