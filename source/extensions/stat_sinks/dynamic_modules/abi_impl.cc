// NOLINT(namespace-envoy)

#include <algorithm>
#include <cstring>

#include "envoy/stats/histogram.h"
#include "envoy/stats/stats.h"

#include "source/common/common/assert.h"
#include "source/common/common/thread.h"
#include "source/common/stats/symbol_table.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/stat_sinks/dynamic_modules/flush_context.h"
#include "source/extensions/stat_sinks/dynamic_modules/sink_config.h"

#include "absl/strings/string_view.h"
#include "absl/types/span.h"

namespace {

using Envoy::Extensions::StatSinks::DynamicModules::DynamicModuleStatsSinkConfig;
using Envoy::Extensions::StatSinks::DynamicModules::DynamicModuleStatsSinkConfigScheduler;
using Envoy::Extensions::StatSinks::DynamicModules::DynamicModuleStatsSinkFlushContext;
using Envoy::Extensions::StatSinks::DynamicModules::DynamicModuleStatsSinkRetainedSnapshot;

DynamicModuleStatsSinkFlushContext*
toFlushContext(envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr ptr) {
  return static_cast<DynamicModuleStatsSinkFlushContext*>(ptr);
}

DynamicModuleStatsSinkConfig*
toStatsSinkConfig(envoy_dynamic_module_type_stat_sink_config_envoy_ptr ptr) {
  return static_cast<DynamicModuleStatsSinkConfig*>(ptr);
}

// Writes up to capacity bytes of src into buffer with no null terminator and reports the full size
// via size_out, so the module can detect truncation and retry with a larger buffer.
void copyToModuleBuffer(absl::string_view src, char* buffer, size_t capacity, size_t* size_out) {
  // A null buffer is only valid as a length query when capacity is 0.
  ASSERT(buffer != nullptr || capacity == 0);
  if (capacity > 0 && !src.empty()) {
    memcpy(buffer, src.data(), std::min(src.size(), capacity)); // NOLINT(safe-memcpy)
  }
  *size_out = src.size();
}

// Resolves the Metric at index from a snapshot collection, or nullptr when out of range. The
// counter collection holds CounterSnapshot (metric under counter_), while the gauge and text
// readout collections hold metric reference wrappers directly; these overloads hide that shape
// difference from the tag helpers below.
const Envoy::Stats::Metric*
metricAt(const std::vector<Envoy::Stats::MetricSnapshot::CounterSnapshot>& counters, size_t index) {
  return index < counters.size() ? &counters[index].counter_.get() : nullptr;
}
template <typename MetricRefs>
const Envoy::Stats::Metric* metricAt(const MetricRefs& metrics, size_t index) {
  return index < metrics.size() ? &metrics[index].get() : nullptr;
}

// Serializes the metric's tag-extracted name directly into the module buffer. Uses the borrowed
// tagExtractedStatName() and serializeToBuffer, so no intermediate std::string is composed (the
// same allocation-free path as the counter/gauge name callbacks). Returns false when index is out
// of range.
template <typename MetricRefs>
bool getTagExtractedName(const MetricRefs& metrics, size_t index, char* name_buffer,
                         size_t name_buffer_capacity, size_t* name_size) {
  const Envoy::Stats::Metric* metric = metricAt(metrics, index);
  if (metric == nullptr) {
    return false;
  }
  *name_size = metric->constSymbolTable().serializeToBuffer(metric->tagExtractedStatName(),
                                                            name_buffer, name_buffer_capacity);
  return true;
}

// Fills the flush context tag cache for the metric if it is not already cached and returns the
// cached tags. A module reads a metric's tags in order, so caching the last metric collapses the
// repeated per-index tag walks from O(T^2) to O(T) for a full read.
const std::vector<Envoy::Stats::StatNameTag>&
cachedTags(DynamicModuleStatsSinkFlushContext& context, const Envoy::Stats::Metric& metric) {
  if (context.cached_metric_ != &metric) {
    // Invalidate before refilling so a failure partway through cannot leave a stale metric matched
    // to a partial tag list.
    context.cached_metric_ = nullptr;
    context.cached_tags_.clear();
    metric.iterateTagStatNames(
        [&context](Envoy::Stats::StatName name, Envoy::Stats::StatName value) {
          context.cached_tags_.emplace_back(name, value);
          return true;
        });
    context.cached_metric_ = &metric;
  }
  return context.cached_tags_;
}

// Reports the number of tags on the metric at index. Uses the flush context tag cache so a full
// per-metric read walks the tags once. Returns false when index is out of range.
template <typename MetricRefs>
bool getTagCount(DynamicModuleStatsSinkFlushContext& context, const MetricRefs& metrics,
                 size_t index, size_t* tag_count) {
  const Envoy::Stats::Metric* metric = metricAt(metrics, index);
  if (metric == nullptr) {
    return false;
  }
  *tag_count = cachedTags(context, *metric).size();
  return true;
}

// Serializes the name and value of one tag directly into the module buffers. Reads the tag pair
// from the flush context tag cache so a full per-metric read walks the tags once. Returns false
// when either index is out of range.
template <typename MetricRefs>
bool getTag(DynamicModuleStatsSinkFlushContext& context, const MetricRefs& metrics, size_t index,
            size_t tag_index, char* name_buffer, size_t name_buffer_capacity, size_t* name_size,
            char* value_buffer, size_t value_buffer_capacity, size_t* value_size) {
  const Envoy::Stats::Metric* metric = metricAt(metrics, index);
  if (metric == nullptr) {
    return false;
  }
  const std::vector<Envoy::Stats::StatNameTag>& tags = cachedTags(context, *metric);
  if (tag_index >= tags.size()) {
    return false;
  }
  const Envoy::Stats::SymbolTable& symbol_table = metric->constSymbolTable();
  *name_size =
      symbol_table.serializeToBuffer(tags[tag_index].first, name_buffer, name_buffer_capacity);
  *value_size =
      symbol_table.serializeToBuffer(tags[tag_index].second, value_buffer, value_buffer_capacity);
  return true;
}

// The value of the counter at index. A retained snapshot reports the value copied at retain time.
uint64_t counterValue(const DynamicModuleStatsSinkFlushContext& context, size_t index,
                      const Envoy::Stats::Counter& counter) {
  return context.frozen_ != nullptr ? context.frozen_->counter_values_[index] : counter.value();
}

// The value of the gauge at index. A retained snapshot reports the value copied at retain time.
uint64_t gaugeValue(const DynamicModuleStatsSinkFlushContext& context, size_t index,
                    const Envoy::Stats::Gauge& gauge) {
  return context.frozen_ != nullptr ? context.frozen_->gauge_values_[index] : gauge.value();
}

// The cumulative values of a histogram. bounds (upper bounds) and counts (cumulative counts) are
// the same length.
struct HistogramValues {
  uint64_t sample_count;
  double sample_sum;
  absl::Span<const double> bounds;
  absl::Span<const uint64_t> counts;
};

// The cumulative values of the histogram at index. A retained snapshot reports the values copied
// at retain time, while the flush hook's handle reads the histogram's cumulative statistics, which
// only the main thread may do.
HistogramValues histogramValues(const DynamicModuleStatsSinkFlushContext& context, size_t index,
                                const Envoy::Stats::ParentHistogram& histogram) {
  if (context.frozen_ != nullptr) {
    const auto& frozen = *context.frozen_;
    const size_t begin = frozen.histogram_bucket_offsets_[index];
    const size_t size = frozen.histogram_bucket_offsets_[index + 1] - begin;
    return {frozen.histogram_sample_counts_[index], frozen.histogram_sample_sums_[index],
            absl::MakeConstSpan(frozen.histogram_bucket_bounds_).subspan(begin, size),
            absl::MakeConstSpan(frozen.histogram_bucket_counts_).subspan(begin, size)};
  }
  const Envoy::Stats::HistogramStatistics& stats = histogram.cumulativeStatistics();
  return {stats.sampleCount(), stats.sampleSum(), stats.supportedBuckets(),
          stats.computedBuckets()};
}

} // namespace

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace DynamicModules {

DynamicModuleStatsSinkRetainedSnapshot::DynamicModuleStatsSinkRetainedSnapshot(
    DynamicModuleStatsSinkFlushContext& source)
    : snapshot_time_(source.snapshot_.snapshotTime()) {
  // The flush hook's handle reads live stats, and its histogram statistics are only stable on the
  // main thread. A retained source only exposes its frozen values and stats it already references,
  // so adding references to them is a lock-free increment from a count of at least one, which is
  // safe on whichever thread holds the source.
  if (source.frozen_ == nullptr) {
    ASSERT_IS_MAIN_OR_TEST_THREAD();
  }
  // Each RefcountPtr built from a raw stat pointer takes one reference through the stat's intrusive
  // count, exactly as Envoy's own snapshot does. The snapshot only hands out const references, and
  // taking a reference does not change anything the snapshot exposes, so casting away const is
  // safe.
  const auto& counters = source.snapshot_.counters();
  snapped_counters_.reserve(counters.size());
  counters_.reserve(counters.size());
  frozen_.counter_values_.reserve(counters.size());
  for (size_t i = 0; i < counters.size(); i++) {
    const Stats::Counter& counter = counters[i].counter_.get();
    snapped_counters_.emplace_back(const_cast<Stats::Counter*>(&counter));
    counters_.push_back({counters[i].delta_, counter});
    frozen_.counter_values_.push_back(counterValue(source, i, counter));
  }

  const auto& gauges = source.snapshot_.gauges();
  snapped_gauges_.reserve(gauges.size());
  gauges_.reserve(gauges.size());
  frozen_.gauge_values_.reserve(gauges.size());
  for (size_t i = 0; i < gauges.size(); i++) {
    const Stats::Gauge& gauge = gauges[i].get();
    snapped_gauges_.emplace_back(const_cast<Stats::Gauge*>(&gauge));
    gauges_.push_back(gauge);
    frozen_.gauge_values_.push_back(gaugeValue(source, i, gauge));
  }

  const auto& text_readouts = source.snapshot_.textReadouts();
  snapped_text_readouts_.reserve(text_readouts.size());
  text_readouts_.reserve(text_readouts.size());
  frozen_.text_readout_values_.reserve(text_readouts.size());
  for (size_t i = 0; i < text_readouts.size(); i++) {
    const Stats::TextReadout& text_readout = text_readouts[i].get();
    snapped_text_readouts_.emplace_back(const_cast<Stats::TextReadout*>(&text_readout));
    text_readouts_.push_back(text_readout);
    frozen_.text_readout_values_.push_back(
        source.frozen_ != nullptr ? source.frozen_->text_readout_values_[i] : text_readout.value());
  }

  // The main thread rewrites cumulative statistics on every merge, so a retained snapshot copies
  // them now. Sizing the flat bucket arrays first keeps this to one allocation each.
  const auto& histograms = source.snapshot_.histograms();
  size_t total_buckets = 0;
  for (size_t i = 0; i < histograms.size(); i++) {
    total_buckets += histogramValues(source, i, histograms[i].get()).bounds.size();
  }
  snapped_histograms_.reserve(histograms.size());
  histograms_.reserve(histograms.size());
  frozen_.histogram_sample_counts_.reserve(histograms.size());
  frozen_.histogram_sample_sums_.reserve(histograms.size());
  frozen_.histogram_bucket_offsets_.reserve(histograms.size() + 1);
  frozen_.histogram_bucket_bounds_.reserve(total_buckets);
  frozen_.histogram_bucket_counts_.reserve(total_buckets);
  frozen_.histogram_bucket_offsets_.push_back(0);
  for (size_t i = 0; i < histograms.size(); i++) {
    const Stats::ParentHistogram& histogram = histograms[i].get();
    snapped_histograms_.emplace_back(const_cast<Stats::ParentHistogram*>(&histogram));
    histograms_.push_back(histogram);
    const HistogramValues values = histogramValues(source, i, histogram);
    frozen_.histogram_sample_counts_.push_back(values.sample_count);
    frozen_.histogram_sample_sums_.push_back(values.sample_sum);
    frozen_.histogram_bucket_bounds_.insert(frozen_.histogram_bucket_bounds_.end(),
                                            values.bounds.begin(), values.bounds.end());
    frozen_.histogram_bucket_counts_.insert(frozen_.histogram_bucket_counts_.end(),
                                            values.counts.begin(), values.counts.end());
    frozen_.histogram_bucket_offsets_.push_back(frozen_.histogram_bucket_bounds_.size());
  }
}

} // namespace DynamicModules
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy

extern "C" {

envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr
envoy_dynamic_module_callback_stat_sink_snapshot_retain(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr) {
  auto* retained = new DynamicModuleStatsSinkRetainedSnapshot(*toFlushContext(snapshot_envoy_ptr));
  return &retained->context();
}

void envoy_dynamic_module_callback_stat_sink_snapshot_release(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr retained_snapshot_envoy_ptr) {
  DynamicModuleStatsSinkRetainedSnapshot* owner =
      toFlushContext(retained_snapshot_envoy_ptr)->owner_;
  // Releasing the borrowed flush handle is a module bug. Ignore it rather than free Envoy's own
  // snapshot.
  ASSERT(owner != nullptr);
  delete owner;
}

size_t envoy_dynamic_module_callback_stat_sink_snapshot_get_counter_count(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr) {
  return toFlushContext(snapshot_envoy_ptr)->snapshot_.counters().size();
}

bool envoy_dynamic_module_callback_stat_sink_snapshot_get_counter(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr, size_t index,
    char* name_buffer, size_t name_buffer_capacity, size_t* name_size, uint64_t* value_out,
    uint64_t* delta_out) {
  auto* context = toFlushContext(snapshot_envoy_ptr);
  const auto& counters = context->snapshot_.counters();
  if (index >= counters.size()) {
    return false;
  }
  const auto& snap = counters[index];
  const Envoy::Stats::Counter& counter = snap.counter_.get();
  *name_size = counter.constSymbolTable().serializeToBuffer(counter.statName(), name_buffer,
                                                            name_buffer_capacity);
  *value_out = counterValue(*context, index, counter);
  *delta_out = snap.delta_;
  return true;
}

size_t envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge_count(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr) {
  return toFlushContext(snapshot_envoy_ptr)->snapshot_.gauges().size();
}

bool envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr, size_t index,
    char* name_buffer, size_t name_buffer_capacity, size_t* name_size, uint64_t* value_out) {
  auto* context = toFlushContext(snapshot_envoy_ptr);
  const auto& gauges = context->snapshot_.gauges();
  if (index >= gauges.size()) {
    return false;
  }
  const Envoy::Stats::Gauge& gauge = gauges[index].get();
  *name_size = gauge.constSymbolTable().serializeToBuffer(gauge.statName(), name_buffer,
                                                          name_buffer_capacity);
  *value_out = gaugeValue(*context, index, gauge);
  return true;
}

size_t envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout_count(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr) {
  return toFlushContext(snapshot_envoy_ptr)->snapshot_.textReadouts().size();
}

bool envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr, size_t index,
    char* name_buffer, size_t name_buffer_capacity, size_t* name_size, char* value_buffer,
    size_t value_buffer_capacity, size_t* value_size) {
  auto* context = toFlushContext(snapshot_envoy_ptr);
  const auto& readouts = context->snapshot_.textReadouts();
  if (index >= readouts.size()) {
    return false;
  }
  const Envoy::Stats::TextReadout& readout = readouts[index].get();
  *name_size = readout.constSymbolTable().serializeToBuffer(readout.statName(), name_buffer,
                                                            name_buffer_capacity);
  if (context->frozen_ != nullptr) {
    copyToModuleBuffer(context->frozen_->text_readout_values_[index], value_buffer,
                       value_buffer_capacity, value_size);
  } else {
    // TextReadout exposes only an owning value() accessor, so copy from the temporary into the
    // module buffer. Names stay allocation-free above.
    copyToModuleBuffer(readout.value(), value_buffer, value_buffer_capacity, value_size);
  }
  return true;
}

size_t envoy_dynamic_module_callback_stat_sink_snapshot_get_histogram_count(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr) {
  return toFlushContext(snapshot_envoy_ptr)->snapshot_.histograms().size();
}

bool envoy_dynamic_module_callback_stat_sink_snapshot_get_histogram(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr, size_t index,
    char* name_buffer, size_t name_buffer_capacity, size_t* name_size, uint64_t* sample_count_out,
    double* sample_sum_out) {
  auto* context = toFlushContext(snapshot_envoy_ptr);
  const auto& histograms = context->snapshot_.histograms();
  if (index >= histograms.size()) {
    return false;
  }
  const Envoy::Stats::ParentHistogram& histogram = histograms[index].get();
  *name_size = histogram.constSymbolTable().serializeToBuffer(histogram.statName(), name_buffer,
                                                              name_buffer_capacity);
  const HistogramValues values = histogramValues(*context, index, histogram);
  *sample_count_out = values.sample_count;
  *sample_sum_out = values.sample_sum;
  return true;
}

size_t envoy_dynamic_module_callback_stat_sink_snapshot_get_histogram_bucket_count(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr,
    size_t histogram_index) {
  auto* context = toFlushContext(snapshot_envoy_ptr);
  const auto& histograms = context->snapshot_.histograms();
  if (histogram_index >= histograms.size()) {
    return 0;
  }
  return histogramValues(*context, histogram_index, histograms[histogram_index].get())
      .bounds.size();
}

bool envoy_dynamic_module_callback_stat_sink_snapshot_get_histogram_bucket(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr,
    size_t histogram_index, size_t bucket_index, double* upper_bound_out,
    uint64_t* cumulative_count_out) {
  auto* context = toFlushContext(snapshot_envoy_ptr);
  const auto& histograms = context->snapshot_.histograms();
  if (histogram_index >= histograms.size()) {
    return false;
  }
  const HistogramValues values =
      histogramValues(*context, histogram_index, histograms[histogram_index].get());
  // The bounds and counts are the same length, so one bound checks both.
  if (bucket_index >= values.bounds.size()) {
    return false;
  }
  *upper_bound_out = values.bounds[bucket_index];
  *cumulative_count_out = values.counts[bucket_index];
  return true;
}

bool envoy_dynamic_module_callback_stat_sink_snapshot_get_histogram_tag_extracted_name(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr, size_t index,
    char* name_buffer, size_t name_buffer_capacity, size_t* name_size) {
  return getTagExtractedName(toFlushContext(snapshot_envoy_ptr)->snapshot_.histograms(), index,
                             name_buffer, name_buffer_capacity, name_size);
}

bool envoy_dynamic_module_callback_stat_sink_snapshot_get_histogram_tag_count(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr, size_t index,
    size_t* tag_count) {
  auto* context = toFlushContext(snapshot_envoy_ptr);
  return getTagCount(*context, context->snapshot_.histograms(), index, tag_count);
}

bool envoy_dynamic_module_callback_stat_sink_snapshot_get_histogram_tag(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr, size_t index,
    size_t tag_index, char* name_buffer, size_t name_buffer_capacity, size_t* name_size,
    char* value_buffer, size_t value_buffer_capacity, size_t* value_size) {
  auto* context = toFlushContext(snapshot_envoy_ptr);
  return getTag(*context, context->snapshot_.histograms(), index, tag_index, name_buffer,
                name_buffer_capacity, name_size, value_buffer, value_buffer_capacity, value_size);
}

bool envoy_dynamic_module_callback_stat_sink_snapshot_get_counter_tag_extracted_name(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr, size_t index,
    char* name_buffer, size_t name_buffer_capacity, size_t* name_size) {
  return getTagExtractedName(toFlushContext(snapshot_envoy_ptr)->snapshot_.counters(), index,
                             name_buffer, name_buffer_capacity, name_size);
}

bool envoy_dynamic_module_callback_stat_sink_snapshot_get_counter_tag_count(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr, size_t index,
    size_t* tag_count) {
  auto* context = toFlushContext(snapshot_envoy_ptr);
  return getTagCount(*context, context->snapshot_.counters(), index, tag_count);
}

bool envoy_dynamic_module_callback_stat_sink_snapshot_get_counter_tag(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr, size_t index,
    size_t tag_index, char* name_buffer, size_t name_buffer_capacity, size_t* name_size,
    char* value_buffer, size_t value_buffer_capacity, size_t* value_size) {
  auto* context = toFlushContext(snapshot_envoy_ptr);
  return getTag(*context, context->snapshot_.counters(), index, tag_index, name_buffer,
                name_buffer_capacity, name_size, value_buffer, value_buffer_capacity, value_size);
}

bool envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge_tag_extracted_name(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr, size_t index,
    char* name_buffer, size_t name_buffer_capacity, size_t* name_size) {
  return getTagExtractedName(toFlushContext(snapshot_envoy_ptr)->snapshot_.gauges(), index,
                             name_buffer, name_buffer_capacity, name_size);
}

bool envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge_tag_count(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr, size_t index,
    size_t* tag_count) {
  auto* context = toFlushContext(snapshot_envoy_ptr);
  return getTagCount(*context, context->snapshot_.gauges(), index, tag_count);
}

bool envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge_tag(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr, size_t index,
    size_t tag_index, char* name_buffer, size_t name_buffer_capacity, size_t* name_size,
    char* value_buffer, size_t value_buffer_capacity, size_t* value_size) {
  auto* context = toFlushContext(snapshot_envoy_ptr);
  return getTag(*context, context->snapshot_.gauges(), index, tag_index, name_buffer,
                name_buffer_capacity, name_size, value_buffer, value_buffer_capacity, value_size);
}

bool envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout_tag_extracted_name(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr, size_t index,
    char* name_buffer, size_t name_buffer_capacity, size_t* name_size) {
  return getTagExtractedName(toFlushContext(snapshot_envoy_ptr)->snapshot_.textReadouts(), index,
                             name_buffer, name_buffer_capacity, name_size);
}

bool envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout_tag_count(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr, size_t index,
    size_t* tag_count) {
  auto* context = toFlushContext(snapshot_envoy_ptr);
  return getTagCount(*context, context->snapshot_.textReadouts(), index, tag_count);
}

bool envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout_tag(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr, size_t index,
    size_t tag_index, char* name_buffer, size_t name_buffer_capacity, size_t* name_size,
    char* value_buffer, size_t value_buffer_capacity, size_t* value_size) {
  auto* context = toFlushContext(snapshot_envoy_ptr);
  return getTag(*context, context->snapshot_.textReadouts(), index, tag_index, name_buffer,
                name_buffer_capacity, name_size, value_buffer, value_buffer_capacity, value_size);
}

envoy_dynamic_module_type_metrics_result
envoy_dynamic_module_callback_stat_sink_config_define_gauge(
    envoy_dynamic_module_type_stat_sink_config_envoy_ptr config_envoy_ptr,
    envoy_dynamic_module_type_module_buffer name, size_t* gauge_id_ptr) {
  absl::string_view name_view(name.ptr, name.length);
  return toStatsSinkConfig(config_envoy_ptr)->defineGauge(name_view, gauge_id_ptr);
}

envoy_dynamic_module_type_metrics_result envoy_dynamic_module_callback_stat_sink_config_set_gauge(
    envoy_dynamic_module_type_stat_sink_config_envoy_ptr config_envoy_ptr, size_t gauge_id,
    uint64_t value) {
  return toStatsSinkConfig(config_envoy_ptr)->setGauge(gauge_id, value);
}

envoy_dynamic_module_type_stat_sink_config_scheduler_module_ptr
envoy_dynamic_module_callback_stat_sink_config_scheduler_new(
    envoy_dynamic_module_type_stat_sink_config_envoy_ptr config_envoy_ptr) {
  return new DynamicModuleStatsSinkConfigScheduler(
      toStatsSinkConfig(config_envoy_ptr)->weak_from_this());
}

void envoy_dynamic_module_callback_stat_sink_config_scheduler_commit(
    envoy_dynamic_module_type_stat_sink_config_scheduler_module_ptr scheduler_module_ptr,
    uint64_t event_id) {
  static_cast<DynamicModuleStatsSinkConfigScheduler*>(scheduler_module_ptr)->commit(event_id);
}

void envoy_dynamic_module_callback_stat_sink_config_scheduler_delete(
    envoy_dynamic_module_type_stat_sink_config_scheduler_module_ptr scheduler_module_ptr) {
  delete static_cast<DynamicModuleStatsSinkConfigScheduler*>(scheduler_module_ptr);
}

} // extern "C"
