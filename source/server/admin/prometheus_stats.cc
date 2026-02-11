#include "source/server/admin/prometheus_stats.h"

#include <cmath>
#include <map>
#include <set>

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
    switch (histogramType()) {
    case HistogramType::Summary:
      generateSummaryOutput(output, histograms, prefixed_tag_extracted_name);
      break;
    case HistogramType::ClassicHistogram:
      generateHistogramOutput(output, histograms, prefixed_tag_extracted_name);
      break;
    case HistogramType::NativeHistogram:
      IS_ENVOY_BUG("invalid type");
      break;
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
  static constexpr uint32_t kDefaultMaxNativeHistogramBuckets = 20;

  ProtobufFormat(absl::optional<uint32_t> native_histogram_max_buckets)
      : native_histogram_max_buckets_(
            native_histogram_max_buckets.value_or(kDefaultMaxNativeHistogramBuckets)) {}

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

    switch (histogramType()) {
    case HistogramType::Summary:
      generateSummaryOutput(metric_family, histograms);
      break;
    case HistogramType::ClassicHistogram:
      generateHistogramOutput(metric_family, histograms);
      break;
    case HistogramType::NativeHistogram:
      generateNativeHistogramOutput(metric_family, histograms);
      break;
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

  // Set zero threshold - values below this go in zero bucket.
  // Since Histogram::recordValue() only accepts integers, the minimum non-zero value is 1.
  // Setting threshold to 0.5 ensures:
  // - Zeros go to zero bucket (0 < 0.5)
  // - Values >= 1 get positive bucket indices (1 > 0.5).
  // Using 0.5 avoids interpolation issues at bucket boundaries that occur with 1.0.
  // For Percent unit histograms, values are scaled by 1/PercentScale, so the threshold
  // must also be scaled accordingly.
  static constexpr double kNativeHistogramZeroThreshold = 0.5;

  static constexpr double nativeHistogramZeroThreshold(Stats::Histogram::Unit unit) {
    return (unit == Stats::Histogram::Unit::Percent)
               ? (kNativeHistogramZeroThreshold / Stats::Histogram::PercentScale)
               : kNativeHistogramZeroThreshold;
  }

  /**
   * Generates Prometheus native histogram output from Envoy's circllhist histograms.
   *
   * References for Prometheus native histogram format:
   *
   * https://prometheus.io/docs/specs/native_histograms/
   * https://docs.google.com/document/d/1VhtB_cGnuO2q_zqEMgtoaLDvJ_kFSXRXoE0Wo74JlSY/edit?tab=t.0
   *
   * Envoy uses circllhist (a log-linear histogram library) internally, which provides ~90 buckets
   * per order of magnitude with very high precision. Prometheus native histograms use exponential
   * buckets with base = 2^(2^(-schema)), where schema ranges from -4 (coarsest, 16x per bucket)
   * to 8 (finest, ~0.27% width per bucket).
   *
   * This code tries to map as accurately as possible from one format to the other.
   */
  void generateNativeHistogramOutput(
      io::prometheus::client::MetricFamily& metric_family,
      const std::vector<const Stats::ParentHistogram*>& histograms) const {
    metric_family.set_type(io::prometheus::client::MetricType::HISTOGRAM);

    for (const auto* histogram : histograms) {
      const Stats::HistogramStatistics& stats = histogram->cumulativeStatistics();

      auto* metric = metric_family.add_metric();
      addLabelsToMetric(metric, histogram->tags());

      auto* proto_histogram = metric->mutable_histogram();

      // Handle empty histogram case early to avoid unnecessary work.
      // Add a no-op span (offset 0, length 0) to distinguish from classic histograms.
      if (stats.sampleCount() == 0) {
        proto_histogram->set_schema(3); // Default schema
        proto_histogram->set_zero_count(0);
        auto* span = proto_histogram->add_positive_span();
        span->set_offset(0);
        span->set_length(0);
        continue;
      }

      proto_histogram->set_sample_count(stats.sampleCount());
      proto_histogram->set_sample_sum(stats.sampleSum());

      const double zero_threshold = nativeHistogramZeroThreshold(histogram->unit());
      proto_histogram->set_zero_threshold(zero_threshold);

      const auto detailed_buckets = histogram->detailedTotalBuckets();
      const auto [schema, needed_indices] = chooseNativeHistogramSchema(
          detailed_buckets, native_histogram_max_buckets_, zero_threshold);
      proto_histogram->set_schema(schema);

      // Count samples below zero_threshold as zero bucket
      const uint64_t zero_count = histogram->cumulativeCountLessThanOrEqualToValue(zero_threshold);
      proto_histogram->set_zero_count(zero_count);
      uint64_t prev_cumulative = zero_count;

      const double base = std::pow(2.0, std::pow(2.0, -schema));

      // Process needed indices and encode directly to protobuf spans and deltas.
      // We iterate over needed_indices, query cumulative counts, and build the
      // span/delta encoding.
      int32_t prev_nonzero_index = 0;
      int64_t prev_count = 0;
      bool first_nonzero = true;
      io::prometheus::client::BucketSpan* current_span = nullptr;
      uint32_t span_length = 0;

      proto_histogram->mutable_positive_delta()->Reserve(needed_indices.size());
      for (int32_t idx : needed_indices) {
        const double upper_bound = std::pow(base, idx + 1);
        uint64_t cumulative = histogram->cumulativeCountLessThanOrEqualToValue(upper_bound);
        uint64_t bucket_count = cumulative - prev_cumulative;
        prev_cumulative = cumulative;

        if (bucket_count == 0) {
          continue; // Skip zero-count buckets; gaps are handled by span encoding
        }

        const bool need_new_span = first_nonzero || (idx != prev_nonzero_index + 1);
        if (need_new_span) {
          if (current_span != nullptr) {
            // Finalize previous span if exists
            current_span->set_length(span_length);
          }

          current_span = proto_histogram->add_positive_span();
          if (first_nonzero) {
            current_span->set_offset(idx); // Offset from 0 for first span
            first_nonzero = false;
          } else {
            current_span->set_offset(idx - prev_nonzero_index - 1); // Gap from previous span
          }
          span_length = 0;
        }

        // Add delta-encoded count: the format takes the difference from the previous bucket
        // value, assuming that adjacent buckets often have similar values, and small numbers
        // encode smaller as protobuf varint.
        int64_t delta = static_cast<int64_t>(bucket_count) - prev_count;
        proto_histogram->add_positive_delta(delta);

        prev_nonzero_index = idx;
        prev_count = static_cast<int64_t>(bucket_count);
        span_length++;
      }

      if (current_span != nullptr) {
        current_span->set_length(span_length);
      }
    }
  }

  // Choose the highest-resolution schema that keeps the bucket count within max_buckets.
  // Returns both the schema and the computed bucket indices to avoid recomputing them.
  static std::pair<int8_t, std::set<int32_t>>
  chooseNativeHistogramSchema(const std::vector<Stats::ParentHistogram::Bucket>& detailed_buckets,
                              uint32_t max_buckets, double zero_threshold) {
    // Schema ranges from -4 (coarsest: 16x per bucket) to 8 (finest: ~0.27% per bucket). However,
    // we cap at schema 5 because circllhist has ~90 buckets per decade, which translates to ~27
    // buckets per doubling. This resolution falls between schema 4 (16 buckets/doubling) and schema
    // 5 (32 buckets/doubling). Using schemas higher than 5 would create artificial precision via
    // interpolation, not real accuracy gains.
    //
    // The default schema used is 4. Often schema 5 is more precision than is required, and because
    // the underlying data is at an accuracy between schemas 4 and 5, choose the lower value to
    // reduce resource usage.

    // Uncomment and use this if schema is every directly specified.
    // constexpr int8_t kSchemaMax = 5;

    constexpr int8_t kSchemaMin = -4;
    constexpr int8_t kSchemaDefault = 4;

    for (int8_t schema = kSchemaDefault; schema >= kSchemaMin; --schema) {
      absl::optional<std::set<int32_t>> indices = nativeHistogramBucketIndicesFromHistogramBuckets(
          detailed_buckets, schema, zero_threshold, max_buckets);
      // If it doesn't have a value, that means it exceeded `max_buckets`.
      if (indices.has_value()) {
        return {schema, std::move(*indices)};
      }
    }
    // Fallback if nothing fits - compute indices at coarsest schema without limit
    return {kSchemaMin, nativeHistogramBucketIndicesFromHistogramBuckets(detailed_buckets,
                                                                         kSchemaMin, zero_threshold)
                            .value()};
  }

  // For the vector of histogram buckets, return the set of all native histogram indices that
  // cover any part of the range of any of the buckets.
  //
  // If max_buckets is provided and the limit would be exceeded, returns nullopt.
  static absl::optional<std::set<int32_t>> nativeHistogramBucketIndicesFromHistogramBuckets(
      const std::vector<Stats::ParentHistogram::Bucket>& buckets, int8_t schema,
      double zero_threshold, absl::optional<uint32_t> max_buckets = absl::nullopt) {
    std::set<int32_t> indices;

    const double log_base = std::log(std::pow(2.0, std::pow(2.0, static_cast<double>(-schema))));

    for (const auto& bucket : buckets) {
      ASSERT(bucket.count_ > 0, "unexpected empty bucket");
      const double upper_bound = bucket.lower_bound_ + bucket.width_;
      if (upper_bound <= zero_threshold) {
        continue; // Entire bucket is in zero bucket range
      }

      ASSERT(bucket.lower_bound_ >= 0, "Envoy histograms only have unsigned integers recorded.");

      // Clamp lower bound to zero_threshold to prevent log(0).
      const double effective_lower = std::max(bucket.lower_bound_, zero_threshold);
      // Use ceil(...) - 1 to find the bucket containing effective_lower.
      // Prometheus bucket i covers (base^i, base^(i+1)], so value v is in bucket
      // ceil(log(v)/log(base)) - 1. This correctly handles boundary cases where
      // v = base^k exactly (it goes in bucket k-1, not k).
      const int32_t lower_index =
          static_cast<int32_t>(std::ceil(std::log(effective_lower) / log_base)) - 1;
      const int32_t upper_index = static_cast<int32_t>(std::ceil(std::log(upper_bound) / log_base));

      for (int32_t idx = lower_index; idx <= upper_index; ++idx) {
        indices.insert(idx);

        // Early termination if we've exceeded the limit
        if (max_buckets.has_value() && indices.size() > *max_buckets) {
          return absl::nullopt;
        }
      }
    }

    return indices;
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

  uint32_t native_histogram_max_buckets_{kDefaultMaxNativeHistogramBuckets};
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

absl::Status PrometheusStatsFormatter::validateParams(const StatsParams& params,
                                                      const Http::RequestHeaderMap& headers) {
  absl::Status result;
  switch (params.histogram_buckets_mode_) {
  case Utility::HistogramBucketsMode::Summary:
  case Utility::HistogramBucketsMode::Unset:
  case Utility::HistogramBucketsMode::Cumulative:
    result = absl::OkStatus();
    break;
  case Utility::HistogramBucketsMode::PrometheusNative:
    if (useProtobufFormat(params, headers)) {
      result = absl::OkStatus();
    } else {
      result = absl::InvalidArgumentError("unsupported prometheusnative histogram type when not "
                                          "using protobuf exposition format");
    }
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

  OutputFormat::HistogramType hist_type;

  // Validation of bucket modes is handled separately.
  switch (params.histogram_buckets_mode_) {
  case Utility::HistogramBucketsMode::Summary:
    hist_type = OutputFormat::HistogramType::Summary;
    break;
  case Utility::HistogramBucketsMode::Unset:
  case Utility::HistogramBucketsMode::Cumulative:
    hist_type = OutputFormat::HistogramType::ClassicHistogram;
    break;
  case Utility::HistogramBucketsMode::PrometheusNative:
    hist_type = OutputFormat::HistogramType::NativeHistogram;
    break;
  // "Detailed" and "Disjoint" don't make sense for prometheus histogram semantics. These types were
  // have been filtered out in validateParams().
  case Utility::HistogramBucketsMode::Detailed:
  case Utility::HistogramBucketsMode::Disjoint:
    hist_type = OutputFormat::HistogramType::ClassicHistogram;
    IS_ENVOY_BUG("unsupported prometheus histogram bucket mode");
    break;
  }

  output_format.setHistogramType(hist_type);

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

  ProtobufFormat output_format(params.native_histogram_max_buckets_);
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
