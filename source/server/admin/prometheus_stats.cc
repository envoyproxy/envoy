#include "source/server/admin/prometheus_stats.h"

#include <cmath>
#include <map>
#include <set>
#include <span>
#include <type_traits>
#include <utility>

#include "source/common/buffer/buffer_impl.h"
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

constexpr absl::string_view kCounter = "counter";
constexpr absl::string_view kGauge = "gauge";
constexpr absl::string_view kHistogram = "histogram";
constexpr absl::string_view kSummary = "summary";

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

// same logic as above, but does it in place (no allocations)
void sanitizeNameInPlace(std::string& name) {
  for (char& c : name) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
      c = '_';
    }
  }
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

// Names and tags remain owned by the metric. Only values are copied for the response.
template <class StatType, class Value> struct TextMetricSnapshot {
  Stats::RefcountPtr<StatType> metric_;
  Value value_;

  Stats::TagVector tags() const { return metric_->tags(); }
  const Value& value() const { return value_; }
};

struct TextHistogramValue {
  uint64_t count_;
  double sum_;
  // Bucket boundaries and quantile levels are immutable and outlive the request.
  const std::vector<double>* bounds_;
  size_t offset_;
};

using TextCounterSnapshot = TextMetricSnapshot<Stats::Counter, uint64_t>;
using TextGaugeSnapshot = TextMetricSnapshot<Stats::Gauge, uint64_t>;
using TextReadoutSnapshot = TextMetricSnapshot<Stats::TextReadout, std::string>;
using TextHistogramSnapshot = TextMetricSnapshot<Stats::ParentHistogram, TextHistogramValue>;

template <class StatType> const StatType* statPointer(const Stats::RefcountPtr<StatType>& stat) {
  return stat.get();
}
template <class StatType, class Value>
const TextMetricSnapshot<StatType, Value>*
statPointer(const TextMetricSnapshot<StatType, Value>& stat) {
  return &stat;
}
template <class StatType> const StatType& statMetadata(const StatType* stat) { return *stat; }
template <class StatType, class Value>
const StatType& statMetadata(const TextMetricSnapshot<StatType, Value>* stat) {
  return *stat->metric_;
}

class TextFormat : public PrometheusStatsFormatter::OutputFormat {
public:
  void setEmitType(bool emit_type) { emit_type_ = emit_type; }

  void setHistogramValues(std::span<const uint64_t> buckets, std::span<const double> quantiles) {
    buckets_ = buckets;
    quantiles_ = quantiles;
  }

  void generateOutput(Buffer::Instance& output,
                      const std::vector<const TextCounterSnapshot*>& metrics,
                      const std::string& name) const {
    generateNumericOutput(output, metrics, name);
  }
  void generateOutput(Buffer::Instance& output,
                      const std::vector<const TextGaugeSnapshot*>& metrics,
                      const std::string& name) const {
    generateNumericOutput(output, metrics, name);
  }
  void generateOutput(Buffer::Instance& output,
                      const std::vector<const TextReadoutSnapshot*>& metrics,
                      const std::string& name) const {
    generateTypeOutput(output, kGauge, name);
    for (const auto* metric : metrics) {
      generateTextReadout(output, metric->tags(), metric->value(), name);
    }
  }
  void generateOutput(Buffer::Instance& output,
                      const std::vector<const TextHistogramSnapshot*>& metrics,
                      const std::string& name) const {
    const bool summary = histogramType() == HistogramType::Summary;
    generateTypeOutput(output, summary ? kSummary : kHistogram, name);
    for (const auto* metric : metrics) {
      const auto& value = metric->value();
      if (summary) {
        generateSummary(output, metric->tags(), *value.bounds_,
                        quantiles_.subspan(value.offset_, value.bounds_->size()), value.sum_,
                        value.count_, name);
      } else {
        generateHistogram(output, metric->tags(), *value.bounds_,
                          buckets_.subspan(value.offset_, value.bounds_->size()), value.sum_,
                          value.count_, name);
      }
    }
  }

  void generateOutput(Buffer::Instance& output, const std::vector<const Stats::Counter*>& counters,
                      const std::string& prefixed_tag_extracted_name) const override {
    generateNumericOutput(output, counters, prefixed_tag_extracted_name);
  }

  void generateOutput(Buffer::Instance& output,
                      std::vector<Stats::PrimitiveCounterSnapshot*>&& counters,
                      const std::string& prefixed_tag_extracted_name) const override {
    generateNumericOutput(output, std::move(counters), prefixed_tag_extracted_name);
  }

  void generateOutput(Buffer::Instance& output, const std::vector<const Stats::Gauge*>& gauges,
                      const std::string& prefixed_tag_extracted_name) const override {
    generateNumericOutput(output, gauges, prefixed_tag_extracted_name);
  }

  void generateOutput(Buffer::Instance& output,
                      std::vector<Stats::PrimitiveGaugeSnapshot*>&& gauges,
                      const std::string& prefixed_tag_extracted_name) const override {
    generateNumericOutput(output, std::move(gauges), prefixed_tag_extracted_name);
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
    generateTypeOutput(output, kGauge, prefixed_tag_extracted_name);

    for (const auto* text_readout : text_readouts) {
      generateTextReadout(output, text_readout->tags(), text_readout->value(),
                          prefixed_tag_extracted_name);
    }
  }

private:
  void generateTextReadout(Buffer::Instance& output, Stats::TagVector tags,
                           const std::string& value, const std::string& name) const {
    tags.push_back(Stats::Tag{"text_value", value});
    output.add(fmt::format("{0}{{{1}}} 0\n", name,
                           PrometheusStatsFormatter::formattedTags(std::move(tags))));
  }
  void generateTypeOutput(Buffer::Instance& output, absl::string_view type,
                          const std::string& prefixed_tag_extracted_name) const {
    if (emit_type_) {
      output.add(fmt::format("# TYPE {0} {1}\n", prefixed_tag_extracted_name, type));
    }
  }

  template <class StatType>
  void generateNumericOutput(Buffer::Instance& output, const std::vector<const StatType*>& metrics,
                             const std::string& prefixed_tag_extracted_name) const {
    absl::string_view type;
    if constexpr (std::is_same_v<Stats::Counter, StatType> ||
                  std::is_same_v<TextCounterSnapshot, StatType>) {
      type = kCounter;
    } else if constexpr (std::is_same_v<Stats::Gauge, StatType> ||
                         std::is_same_v<TextGaugeSnapshot, StatType>) {
      type = kGauge;
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

  template <class StatType>
  void generateNumericOutput(Buffer::Instance& output, std::vector<StatType*>&& metrics,
                             const std::string& prefixed_tag_extracted_name) const {
    absl::string_view type;
    if constexpr (std::is_same_v<Stats::PrimitiveCounterSnapshot, StatType>) {
      type = kCounter;
    } else if constexpr (std::is_same_v<Stats::PrimitiveGaugeSnapshot, StatType>) {
      type = kGauge;
    } else {
      static_assert(false, "Unexpected StatsType");
    }

    generateTypeOutput(output, type, prefixed_tag_extracted_name);
    for (auto* metric : metrics) {
      const std::string formatted_tags =
          PrometheusStatsFormatter::formattedTags(metric->getAndClearTags());
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
    generateTypeOutput(output, kHistogram, prefixed_tag_extracted_name);

    for (const auto* histogram : histograms) {
      const Stats::HistogramStatistics& stats = histogram->cumulativeStatistics();
      generateHistogram(output, histogram->tags(), stats.supportedBuckets(),
                        stats.computedBuckets(), stats.sampleSum(), stats.sampleCount(),
                        prefixed_tag_extracted_name);
    }
  }

  void generateHistogram(Buffer::Instance& output, Stats::TagVector histogram_tags,
                         std::span<const double> supported_buckets,
                         std::span<const uint64_t> computed_buckets, double sum, uint64_t count,
                         const std::string& prefixed_tag_extracted_name) const {
    const bool empty_tags = histogram_tags.empty();

    const std::string tags = PrometheusStatsFormatter::formattedTags(std::move(histogram_tags));
    const std::string hist_tags = empty_tags ? EMPTY_STRING : (tags + ",");

    for (size_t i = 0; i < supported_buckets.size(); ++i) {
      double bucket = supported_buckets[i];
      uint64_t value = computed_buckets[i];
      // We want to print the bucket in a fixed point (non-scientific) format. The fmt library
      // doesn't have a specific modifier to format as a fixed-point value only so we use the
      // 'g' operator which prints the number in general fixed point format or scientific format
      // with precision 50 to round the number up to 32 significant digits in fixed point format
      // which should cover pretty much all cases
      output.add(fmt::format("{0}_bucket{{{1}le=\"{2:.32g}\"}} {3}\n", prefixed_tag_extracted_name,
                             hist_tags, bucket, value));
    }

    output.add(fmt::format("{0}_bucket{{{1}le=\"+Inf\"}} {2}\n", prefixed_tag_extracted_name,
                           hist_tags, count));
    output.add(fmt::format("{0}_sum{{{1}}} {2:.32g}\n", prefixed_tag_extracted_name, tags, sum));
    output.add(fmt::format("{0}_count{{{1}}} {2}\n", prefixed_tag_extracted_name, tags, count));
  }

  /*
   * Returns the prometheus output for a summary. The output is a multi-line string (with embedded
   * newlines) that contains all the individual quantile values and sum/count for a single histogram
   * (metric_name plus all tags).
   */
  void generateSummaryOutput(Buffer::Instance& output,
                             const std::vector<const Stats::ParentHistogram*>& histograms,
                             const std::string& prefixed_tag_extracted_name) const {
    generateTypeOutput(output, kSummary, prefixed_tag_extracted_name);

    for (const auto* histogram : histograms) {
      const Stats::HistogramStatistics& stats = histogram->intervalStatistics();
      generateSummary(output, histogram->tags(), stats.supportedQuantiles(),
                      stats.computedQuantiles(), stats.sampleSum(), stats.sampleCount(),
                      prefixed_tag_extracted_name);
    }
  }

  void generateSummary(Buffer::Instance& output, Stats::TagVector histogram_tags,
                       std::span<const double> supported_quantiles,
                       std::span<const double> computed_quantiles, double sum, uint64_t count,
                       const std::string& prefixed_tag_extracted_name) const {
    const bool empty_tags = histogram_tags.empty();
    const std::string tags = PrometheusStatsFormatter::formattedTags(std::move(histogram_tags));
    const std::string hist_tags = empty_tags ? EMPTY_STRING : (tags + ",");
    for (size_t i = 0; i < supported_quantiles.size(); ++i) {
      double quantile = supported_quantiles[i];
      double value = computed_quantiles[i];
      output.add(fmt::format("{0}{{{1}quantile=\"{2}\"}} {3:.32g}\n", prefixed_tag_extracted_name,
                             hist_tags, quantile, value));
    }

    output.add(fmt::format("{0}_sum{{{1}}} {2:.32g}\n", prefixed_tag_extracted_name, tags, sum));
    output.add(fmt::format("{0}_count{{{1}}} {2}\n", prefixed_tag_extracted_name, tags, count));
  }
  bool emit_type_{true};
  std::span<const uint64_t> buckets_;
  std::span<const double> quantiles_;
};

class ProtobufFormat : public PrometheusStatsFormatter::OutputFormat {
public:
  static constexpr uint32_t kDefaultMaxNativeHistogramBuckets = 20;

  ProtobufFormat(std::optional<uint32_t> native_histogram_max_buckets)
      : native_histogram_max_buckets_(
            native_histogram_max_buckets.value_or(kDefaultMaxNativeHistogramBuckets)) {}

  void generateOutput(Buffer::Instance& output, const std::vector<const Stats::Counter*>& counters,
                      const std::string& prefixed_tag_extracted_name) const override {
    generateNumericOutput(output, counters, prefixed_tag_extracted_name,
                          io::prometheus::client::MetricType::COUNTER);
  }

  // Return the prometheus output for a group of PrimitiveCounters.
  void generateOutput(Buffer::Instance& output,
                      std::vector<Stats::PrimitiveCounterSnapshot*>&& counters,
                      const std::string& prefixed_tag_extracted_name) const override {
    generateNumericOutput(output, std::move(counters), prefixed_tag_extracted_name,
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
                      std::vector<Stats::PrimitiveGaugeSnapshot*>&& gauges,
                      const std::string& prefixed_tag_extracted_name) const override {
    generateNumericOutput(output, std::move(gauges), prefixed_tag_extracted_name,
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
                         std::vector<Stats::Tag>&& tags) const {
    metric->mutable_label()->Reserve(tags.size());
    for (auto& tag : tags) {
      auto* label = metric->add_label();
      sanitizeNameInPlace(tag.name_);
      label->set_name(std::move(tag.name_));
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

  template <class StatType>
  void generateNumericOutput(Buffer::Instance& output, std::vector<StatType*>&& metrics,
                             const std::string& prefixed_tag_extracted_name,
                             io::prometheus::client::MetricType type) const {
    ASSERT(!metrics.empty());

    io::prometheus::client::MetricFamily metric_family;
    metric_family.set_name(prefixed_tag_extracted_name);
    metric_family.set_type(type);
    metric_family.mutable_metric()->Reserve(metrics.size());

    for (auto* metric : metrics) {
      auto* prom_metric = metric_family.add_metric();

      uint64_t value = metric->value();
      auto tags = metric->getAndClearTags();

      addLabelsToMetric(prom_metric, std::move(tags));

      // Set value based on type
      if (type == io::prometheus::client::MetricType::COUNTER) {
        auto* counter = prom_metric->mutable_counter();
        counter->set_value(value);
      } else {
        auto* gauge = prom_metric->mutable_gauge();
        gauge->set_value(value);
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
      std::optional<std::set<int32_t>> indices = nativeHistogramBucketIndicesFromHistogramBuckets(
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
  static std::optional<std::set<int32_t>> nativeHistogramBucketIndicesFromHistogramBuckets(
      const std::vector<Stats::ParentHistogram::Bucket>& buckets, int8_t schema,
      double zero_threshold, std::optional<uint32_t> max_buckets = std::nullopt) {
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
          return std::nullopt;
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
    std::ignore = message.SerializeWithCachedSizesToArray(end_of_varint);

    ASSERT(end_of_varint >= reservation_start);
    const size_t varint_size = end_of_varint - reservation_start;
    ASSERT(varint_size <= kMaxVarintLength);
    reservation.commit(varint_size + length);
  }

  uint32_t native_histogram_max_buckets_{kDefaultMaxNativeHistogramBuckets};
};

/**
 * Visits globally grouped metrics in stable order. The caller either serializes each complete
 * family or retains its metric pointers for incremental rendering. Ownership remains with metrics.
 */
template <class StatType, class Visit>
uint64_t visitStatType(const StatsParams& params, const std::vector<StatType>& metrics,
                       const Stats::CustomStatNamespaces& custom_namespaces, Visit visit,
                       bool filter_metrics = true) {

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
  using StatTypeUnsortedCollection = std::vector<decltype(statPointer(metrics.front()))>;

  // Return early to avoid crashing when getting the symbol table from the first metric.
  if (metrics.empty()) {
    return 0;
  }

  // There should only be one symbol table for all of the stats in the admin
  // interface. If this assumption changes, the name comparisons in this function
  // will have to change to compare to convert all StatNames to strings before
  // comparison.
  const Stats::SymbolTable& global_symbol_table =
      statMetadata(statPointer(metrics.front())).constSymbolTable();

  // Collection of metrics by their tagExtractedName.
  // Sorting will be done on the names separately.
  absl::flat_hash_map<Stats::StatName, StatTypeUnsortedCollection> groups;

  for (const auto& entry : metrics) {
    const auto* sample = statPointer(entry);
    const auto& metric = statMetadata(sample);
    ASSERT(&global_symbol_table == &metric.constSymbolTable());
    if (filter_metrics && !params.shouldShowMetric(metric)) {
      continue;
    }
    groups[metric.tagExtractedStatName()].push_back(sample);
  }

  std::vector<Stats::StatName> sorted_stat_names;
  sorted_stat_names.reserve(groups.size());
  for (const auto& [group, _] : groups) {
    sorted_stat_names.push_back(group);
  }
  Stats::StatNameLessThan comp(global_symbol_table);
  std::sort(sorted_stat_names.begin(), sorted_stat_names.end(), comp);

  auto result = groups.size();
  for (auto& group_name : sorted_stat_names) {
    auto& group = groups[group_name];
    const std::optional<std::string> prefixed_tag_extracted_name =
        PrometheusStatsFormatter::metricName(global_symbol_table.toString(group_name),
                                             custom_namespaces);
    if (!prefixed_tag_extracted_name.has_value()) {
      --result;
      continue;
    }

    // Sort before producing the final output to satisfy the "preferred" ordering from the
    // prometheus spec: metrics will be sorted by their tags' textual representation, which will
    // be consistent across calls.
    std::sort(group.begin(), group.end(), [](const auto* a, const auto* b) {
      return MetricLessThan()(&statMetadata(a), &statMetadata(b));
    });

    visit(std::move(group), prefixed_tag_extracted_name.value());
  }
  return result;
}

template <class StatType, class Visit>
uint64_t visitPrimitiveStatType(const StatsParams& params, std::vector<StatType>& metrics,
                                const Stats::CustomStatNamespaces& custom_namespaces, Visit visit) {

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
  using StatTypeUnsortedCollection = std::vector<StatType*>;

  // Return early to avoid crashing when getting the symbol table from the first metric.
  if (metrics.empty()) {
    return 0;
  }

  // Collection of metrics sorted by their tagExtractedName.
  // We satisfy the requirements of the exposition format by iterating over the sorted keys.
  absl::flat_hash_map<std::string, StatTypeUnsortedCollection> groups;

  for (auto& metric : metrics) {
    if (!params.shouldShowMetric(metric)) {
      continue;
    }
    groups[metric.tagExtractedName()].push_back(&metric);
  }

  std::vector<std::string> sorted_group_names;
  sorted_group_names.reserve(groups.size());
  for (const auto& [group, _] : groups) {
    sorted_group_names.push_back(group);
  }
  std::sort(sorted_group_names.begin(), sorted_group_names.end());

  auto result = groups.size();
  for (auto& group_name : sorted_group_names) {
    auto& group = groups[group_name];
    const std::optional<std::string> prefixed_tag_extracted_name =
        PrometheusStatsFormatter::metricName(std::move(group_name), custom_namespaces);
    if (!prefixed_tag_extracted_name.has_value()) {
      --result;
      continue;
    }

    // Sort before producing the final output to satisfy the "preferred" ordering from the
    // prometheus spec: metrics will be sorted by their tags' textual representation, which will
    // be consistent across calls.
    std::sort(group.begin(), group.end(), PrimitiveMetricSnapshotLessThan());

    visit(std::move(group), prefixed_tag_extracted_name.value());
  }
  return result;
}

class TextStatCursor {
public:
  virtual ~TextStatCursor() = default;
  virtual bool next(Buffer::Instance& output, TextFormat& format) PURE;
};

template <class StatType> class TextStatCursorImpl : public TextStatCursor {
public:
  void addFamily(std::vector<StatType*>&& metrics, const std::string& name) {
    families_.push_back({std::move(metrics), name});
  }

  bool next(Buffer::Instance& output, TextFormat& format) override {
    if (family_ == families_.size()) {
      return false;
    }
    const auto& family = families_[family_];
    format.setEmitType(metric_ == 0);
    format.generateOutput(output, std::vector<StatType*>{family.metrics_[metric_++]}, family.name_);
    if (metric_ == family.metrics_.size()) {
      metric_ = 0;
      ++family_;
    }
    return true;
  }

private:
  struct Family {
    std::vector<StatType*> metrics_;
    std::string name_;
  };
  std::vector<Family> families_;
  size_t family_{0};
  size_t metric_{0};
};

// Requests are created and driven on the admin dispatcher. Values and query filtering decisions
// are captured before returning the request, so later chunks never sample live metric values.
// Worker threads can still update counters during capture; this is not an atomic store snapshot.
class PrometheusTextRequest : public Admin::Request {
public:
  PrometheusTextRequest(const std::vector<Stats::CounterSharedPtr>& counters,
                        const std::vector<Stats::GaugeSharedPtr>& gauges,
                        const std::vector<Stats::ParentHistogramSharedPtr>& histograms,
                        const std::vector<Stats::TextReadoutSharedPtr>& text_readouts,
                        const Upstream::ClusterManager& cluster_manager, const StatsParams& params,
                        const Stats::CustomStatNamespaces& custom_namespaces, uint64_t chunk_size)
      : counters_(captureValues(counters, params)), gauges_(captureValues(gauges, params)),
        text_readouts_(captureValues(text_readouts, params)), params_(params),
        custom_namespaces_(custom_namespaces), chunk_size_(chunk_size) {
    ASSERT(chunk_size_ > 0);
    format_.setHistogramType(
        params.histogram_buckets_mode_ == Utility::HistogramBucketsMode::Summary
            ? PrometheusStatsFormatter::OutputFormat::HistogramType::Summary
            : PrometheusStatsFormatter::OutputFormat::HistogramType::ClassicHistogram);
    captureHistograms(histograms);
    format_.setHistogramValues(histogram_buckets_, histogram_quantiles_);
    Upstream::HostUtility::forEachHostMetric(
        cluster_manager,
        [&](Stats::PrimitiveCounterSnapshot&& metric) {
          host_counters_.push_back(std::move(metric));
        },
        [&](Stats::PrimitiveGaugeSnapshot&& metric) { host_gauges_.push_back(std::move(metric)); });
  }

  Http::Code start(Http::ResponseHeaderMap&) override { return Http::Code::OK; }

  bool nextChunk(Buffer::Instance& response) override {
    uint64_t remaining = chunk_size_;
    while (remaining != 0) {
      if (pending_.length() != 0) {
        const uint64_t size = std::min(remaining, pending_.length());
        response.move(pending_, size);
        remaining -= size;
        continue;
      }
      if (cursor_ != nullptr && cursor_->next(pending_, format_)) {
        continue;
      }
      cursor_.reset();
      if (!nextPhase()) {
        return false;
      }
    }
    return true;
  }

private:
  enum class Phase { Counters, Gauges, TextReadouts, Histograms, HostCounters, HostGauges, Done };

  template <class StatType>
  static std::vector<TextMetricSnapshot<StatType, decltype(std::declval<StatType>().value())>>
  captureValues(const std::vector<Stats::RefcountPtr<StatType>>& metrics,
                const StatsParams& params) {
    using Value = decltype(metrics.front()->value());
    std::vector<TextMetricSnapshot<StatType, Value>> snapshots;
    snapshots.reserve(metrics.size());
    for (const auto& metric : metrics) {
      if (params.shouldShowMetric(*metric)) {
        snapshots.push_back({metric, metric->value()});
      }
    }
    return snapshots;
  }

  void captureHistograms(const std::vector<Stats::ParentHistogramSharedPtr>& histograms) {
    const bool summary = params_.histogram_buckets_mode_ == Utility::HistogramBucketsMode::Summary;
    size_t values_size = 0;
    histograms_.reserve(histograms.size());
    for (const auto& histogram : histograms) {
      if (!params_.shouldShowMetric(*histogram)) {
        continue;
      }
      const auto& stats =
          summary ? histogram->intervalStatistics() : histogram->cumulativeStatistics();
      const auto& bounds = summary ? stats.supportedQuantiles() : stats.supportedBuckets();
      const size_t max_size =
          summary ? histogram_quantiles_.max_size() : histogram_buckets_.max_size();
      RELEASE_ASSERT(bounds.size() <= max_size - values_size, "histogram snapshot is too large");
      histograms_.push_back(
          {histogram, {stats.sampleCount(), stats.sampleSum(), &bounds, values_size}});
      values_size += bounds.size();
    }

    // Allocate one flat array for all selected histograms.
    // Both passes execute synchronously on the main/admin dispatcher, where parent histogram
    // statistics are updated. Without yielding between passes, count/sum and bucket/quantile
    // values come from the same merged state.
    if (summary) {
      histogram_quantiles_.resize(values_size);
    } else {
      histogram_buckets_.resize(values_size);
    }
    for (const auto& snapshot : histograms_) {
      if (summary) {
        const auto& values = snapshot.metric_->intervalStatistics().computedQuantiles();
        ASSERT(values.size() == snapshot.value_.bounds_->size());
        std::copy(values.begin(), values.end(),
                  histogram_quantiles_.begin() + snapshot.value_.offset_);
      } else {
        const auto& values = snapshot.metric_->cumulativeStatistics().computedBuckets();
        ASSERT(values.size() == snapshot.value_.bounds_->size());
        std::copy(values.begin(), values.end(),
                  histogram_buckets_.begin() + snapshot.value_.offset_);
      }
    }
  }

  template <class StatType> void prepare(const std::vector<StatType>& metrics) {
    using Sample = std::remove_pointer_t<decltype(statPointer(metrics.front()))>;
    auto cursor = std::make_unique<TextStatCursorImpl<Sample>>();
    visitStatType(
        params_, metrics, custom_namespaces_,
        [&](auto&& group, const auto& name) { cursor->addFamily(std::move(group), name); },
        false); // Inclusion was captured with the values, not re-evaluated between chunks.
    cursor_ = std::move(cursor);
  }

  template <class StatType> void preparePrimitive(std::vector<StatType>& metrics) {
    auto cursor = std::make_unique<TextStatCursorImpl<StatType>>();
    visitPrimitiveStatType(
        params_, metrics, custom_namespaces_,
        [&](auto&& group, const auto& name) { cursor->addFamily(std::move(group), name); });
    cursor_ = std::move(cursor);
  }

  bool nextPhase() {
    switch (phase_) {
    case Phase::Counters:
      prepare(counters_);
      phase_ = Phase::Gauges;
      break;
    case Phase::Gauges:
      prepare(gauges_);
      phase_ = Phase::TextReadouts;
      break;
    case Phase::TextReadouts:
      prepare(text_readouts_);
      phase_ = Phase::Histograms;
      break;
    case Phase::Histograms:
      prepare(histograms_);
      phase_ = Phase::HostCounters;
      break;
    case Phase::HostCounters:
      preparePrimitive(host_counters_);
      phase_ = Phase::HostGauges;
      break;
    case Phase::HostGauges:
      preparePrimitive(host_gauges_);
      phase_ = Phase::Done;
      break;
    case Phase::Done:
      return false;
    }
    return true;
  }

  const std::vector<TextCounterSnapshot> counters_;
  const std::vector<TextGaugeSnapshot> gauges_;
  const std::vector<TextReadoutSnapshot> text_readouts_;
  std::vector<TextHistogramSnapshot> histograms_;
  std::vector<uint64_t> histogram_buckets_;
  std::vector<double> histogram_quantiles_;
  const StatsParams params_;
  const Stats::CustomStatNamespaces& custom_namespaces_;
  const uint64_t chunk_size_;
  std::vector<Stats::PrimitiveCounterSnapshot> host_counters_;
  std::vector<Stats::PrimitiveGaugeSnapshot> host_gauges_;
  std::unique_ptr<TextStatCursor> cursor_;
  TextFormat format_;
  // A histogram is serialized once, including all bucket/sum/count lines, before it is drained
  // across chunks. One unusually large metric can exceed the chunk size; an entire family is
  // never materialized here.
  Buffer::OwnedImpl pending_;
  Phase phase_{Phase::Counters};
};

template <class StatType>
uint64_t outputStatType(Buffer::Instance& response, const StatsParams& params,
                        const std::vector<Stats::RefcountPtr<StatType>>& metrics,
                        const PrometheusStatsFormatter::OutputFormat& output_format,
                        const Stats::CustomStatNamespaces& custom_namespaces) {
  return visitStatType(params, metrics, custom_namespaces, [&](auto&& group, const auto& name) {
    output_format.generateOutput(response, group, name);
  });
}

template <class StatType>
uint64_t outputPrimitiveStatType(Buffer::Instance& response, const StatsParams& params,
                                 std::vector<StatType>&& metrics,
                                 const PrometheusStatsFormatter::OutputFormat& output_format,
                                 const Stats::CustomStatNamespaces& custom_namespaces) {
  return visitPrimitiveStatType(params, metrics, custom_namespaces,
                                [&](auto&& group, const auto& name) {
                                  output_format.generateOutput(response, std::move(group), name);
                                });
}

} // namespace

Admin::RequestPtr PrometheusStatsFormatter::makeTextRequest(
    const std::vector<Stats::CounterSharedPtr>& counters,
    const std::vector<Stats::GaugeSharedPtr>& gauges,
    const std::vector<Stats::ParentHistogramSharedPtr>& histograms,
    const std::vector<Stats::TextReadoutSharedPtr>& text_readouts,
    const Upstream::ClusterManager& cluster_manager, const StatsParams& params,
    const Stats::CustomStatNamespaces& custom_namespaces, uint64_t chunk_size) {
  return std::make_unique<PrometheusTextRequest>(counters, gauges, histograms, text_readouts,
                                                 cluster_manager, params, custom_namespaces,
                                                 chunk_size);
}

// Determine the format based on Accept header, using first-match priority.
// Per HTTP spec, clients SHOULD send media types in priority order.
// Text format is only selected if explicitly requested as version 0.0.4 or as fallback.
// Returns true if protobuf format should be used, false for text format.
bool PrometheusStatsFormatter::useProtobufFormat(const StatsParams& params,
                                                 const Http::RequestHeaderMap& headers) {
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

std::string PrometheusStatsFormatter::formattedTags(std::vector<Stats::Tag>&& tags) {
  std::vector<std::string> buf;
  buf.reserve(tags.size());
  for (Stats::Tag& tag : tags) {
    sanitizeNameInPlace(tag.name_);
    buf.push_back(fmt::format("{}=\"{}\"", tag.name_, sanitizeValue(tag.value_)));
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

std::optional<std::string>
PrometheusStatsFormatter::metricName(std::string&& extracted_name,
                                     const Stats::CustomStatNamespaces& custom_namespaces) {
  const std::optional<absl::string_view> custom_namespace_stripped =
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
      return std::nullopt;
    }
    return sanitized_name;
  }

  // If it does not have a custom namespace, add namespacing prefix to avoid conflicts, as per best
  // practice: https://prometheus.io/docs/practices/naming/#metric-names Also, naming conventions on
  // https://prometheus.io/docs/concepts/data_model/
  sanitizeNameInPlace(extracted_name);
  return absl::StrCat("envoy_", extracted_name);
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

  metric_name_count += outputPrimitiveStatType(response, params, std::move(host_counters),
                                               output_format, custom_namespaces);
  metric_name_count += outputPrimitiveStatType(response, params, std::move(host_gauges),
                                               output_format, custom_namespaces);

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
