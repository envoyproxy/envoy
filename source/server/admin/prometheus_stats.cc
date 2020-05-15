#include "server/admin/prometheus_stats.h"

#include "common/common/empty_string.h"
#include "common/stats/histogram_impl.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Server {

namespace {

const std::regex& promRegex() { CONSTRUCT_ON_FIRST_USE(std::regex, "[^a-zA-Z0-9_]"); }

/**
 * Take a string and sanitize it according to Prometheus conventions.
 */
std::string sanitizeName(const std::string& name) {
  // The name must match the regex [a-zA-Z_][a-zA-Z0-9_]* as required by
  // prometheus. Refer to https://prometheus.io/docs/concepts/data_model/.
  std::string stats_name = std::regex_replace(name, promRegex(), "_");
  if (stats_name[0] >= '0' && stats_name[0] <= '9') {
    return absl::StrCat("_", stats_name);
  } else {
    return stats_name;
  }
}

/*
 * Determine whether a metric has never been emitted and choose to
 * not show it if we only wanted used metrics.
 */
template <class StatType>
static bool shouldShowMetric(const StatType& metric, const bool used_only,
                             const absl::optional<std::regex>& regex) {
  return ((!used_only || metric.used()) &&
          (!regex.has_value() || std::regex_search(metric.name(), regex.value())));
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
    Buffer::Instance& response, const bool used_only, const absl::optional<std::regex>& regex,
    const std::vector<Stats::RefcountPtr<StatType>>& metrics,
    const std::function<std::string(
        const StatType& metric, const std::string& prefixed_tag_extracted_name)>& generate_output,
    absl::string_view type) {

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

    if (!shouldShowMetric(*metric, used_only, regex)) {
      continue;
    }

    groups[metric->tagExtractedStatName()].push_back(metric.get());
  }

  for (auto& group : groups) {
    const std::string prefixed_tag_extracted_name =
        PrometheusStatsFormatter::metricName(global_symbol_table.toString(group.first));
    response.add(fmt::format("# TYPE {0} {1}\n", prefixed_tag_extracted_name, type));

    // Sort before producing the final output to satisfy the "preferred" ordering from the
    // prometheus spec: metrics will be sorted by their tags' textual representation, which will
    // be consistent across calls.
    std::sort(group.second.begin(), group.second.end(), MetricLessThan());

    for (const auto& metric : group.second) {
      response.add(generate_output(*metric, prefixed_tag_extracted_name));
    }
    response.add("\n");
  }
  return groups.size();
}

/*
 * Return the prometheus output for a numeric Stat (Counter or Gauge).
 */
template <class StatType>
std::string generateNumericOutput(const StatType& metric,
                                  const std::string& prefixed_tag_extracted_name) {
  const std::string tags = PrometheusStatsFormatter::formattedTags(metric.tags());
  return fmt::format("{0}{{{1}}} {2}\n", prefixed_tag_extracted_name, tags, metric.value());
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
  const std::vector<double>& supported_buckets = stats.supportedBuckets();
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

} // namespace

std::string PrometheusStatsFormatter::formattedTags(const std::vector<Stats::Tag>& tags) {
  std::vector<std::string> buf;
  buf.reserve(tags.size());
  for (const Stats::Tag& tag : tags) {
    buf.push_back(fmt::format("{}=\"{}\"", sanitizeName(tag.name_), tag.value_));
  }
  return absl::StrJoin(buf, ",");
}

std::string PrometheusStatsFormatter::metricName(const std::string& extracted_name) {
  // Add namespacing prefix to avoid conflicts, as per best practice:
  // https://prometheus.io/docs/practices/naming/#metric-names
  // Also, naming conventions on https://prometheus.io/docs/concepts/data_model/
  return sanitizeName(fmt::format("envoy_{0}", extracted_name));
}

// TODO(efimki): Add support of text readouts stats.
uint64_t PrometheusStatsFormatter::statsAsPrometheus(
    const std::vector<Stats::CounterSharedPtr>& counters,
    const std::vector<Stats::GaugeSharedPtr>& gauges,
    const std::vector<Stats::ParentHistogramSharedPtr>& histograms, Buffer::Instance& response,
    const bool used_only, const absl::optional<std::regex>& regex) {

  uint64_t metric_name_count = 0;
  metric_name_count += outputStatType<Stats::Counter>(
      response, used_only, regex, counters, generateNumericOutput<Stats::Counter>, "counter");

  metric_name_count += outputStatType<Stats::Gauge>(response, used_only, regex, gauges,
                                                    generateNumericOutput<Stats::Gauge>, "gauge");

  metric_name_count += outputStatType<Stats::ParentHistogram>(
      response, used_only, regex, histograms, generateHistogramOutput, "histogram");

  return metric_name_count;
}

} // namespace Server
} // namespace Envoy
