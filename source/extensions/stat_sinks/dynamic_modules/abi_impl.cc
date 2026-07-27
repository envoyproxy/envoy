// NOLINT(namespace-envoy)

#include <algorithm>
#include <cstring>

#include "envoy/stats/stats.h"

#include "source/common/common/assert.h"
#include "source/common/stats/symbol_table.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/stat_sinks/dynamic_modules/flush_context.h"
#include "source/extensions/stat_sinks/dynamic_modules/sink_config.h"

#include "absl/strings/string_view.h"

namespace {

using Envoy::Extensions::StatSinks::DynamicModules::DynamicModuleStatsSinkConfig;
using Envoy::Extensions::StatSinks::DynamicModules::DynamicModuleStatsSinkConfigScheduler;
using Envoy::Extensions::StatSinks::DynamicModules::DynamicModuleStatsSinkFlushContext;

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

// Reports the number of tags on the metric at index. Counts via iterateTagStatNames so the tag
// StatNames are not materialized into a std::string vector. Returns false when index is out of
// range.
template <typename MetricRefs>
bool getTagCount(const MetricRefs& metrics, size_t index, size_t* tag_count) {
  const Envoy::Stats::Metric* metric = metricAt(metrics, index);
  if (metric == nullptr) {
    return false;
  }
  size_t count = 0;
  metric->iterateTagStatNames([&count](Envoy::Stats::StatName, Envoy::Stats::StatName) {
    ++count;
    return true;
  });
  *tag_count = count;
  return true;
}

// Serializes the name and value of one tag directly into the module buffers. Iterates the borrowed
// tag StatNames and serializeToBuffers the requested pair, so no std::string is composed. Returns
// false when either index is out of range.
template <typename MetricRefs>
bool getTag(const MetricRefs& metrics, size_t index, size_t tag_index, char* name_buffer,
            size_t name_buffer_capacity, size_t* name_size, char* value_buffer,
            size_t value_buffer_capacity, size_t* value_size) {
  const Envoy::Stats::Metric* metric = metricAt(metrics, index);
  if (metric == nullptr) {
    return false;
  }
  const Envoy::Stats::SymbolTable& symbol_table = metric->constSymbolTable();
  size_t current = 0;
  bool found = false;
  metric->iterateTagStatNames([&](Envoy::Stats::StatName tag_name,
                                  Envoy::Stats::StatName tag_value) {
    if (current == tag_index) {
      *name_size = symbol_table.serializeToBuffer(tag_name, name_buffer, name_buffer_capacity);
      *value_size = symbol_table.serializeToBuffer(tag_value, value_buffer, value_buffer_capacity);
      found = true;
      return false;
    }
    ++current;
    return true;
  });
  return found;
}

} // namespace

extern "C" {

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
  *value_out = counter.value();
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
  *value_out = gauge.value();
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
  // TextReadout exposes only an owning value() accessor, so copy from the temporary into the
  // module buffer. Names stay allocation-free above.
  copyToModuleBuffer(readout.value(), value_buffer, value_buffer_capacity, value_size);
  return true;
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
  return getTagCount(toFlushContext(snapshot_envoy_ptr)->snapshot_.counters(), index, tag_count);
}

bool envoy_dynamic_module_callback_stat_sink_snapshot_get_counter_tag(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr, size_t index,
    size_t tag_index, char* name_buffer, size_t name_buffer_capacity, size_t* name_size,
    char* value_buffer, size_t value_buffer_capacity, size_t* value_size) {
  return getTag(toFlushContext(snapshot_envoy_ptr)->snapshot_.counters(), index, tag_index,
                name_buffer, name_buffer_capacity, name_size, value_buffer, value_buffer_capacity,
                value_size);
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
  return getTagCount(toFlushContext(snapshot_envoy_ptr)->snapshot_.gauges(), index, tag_count);
}

bool envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge_tag(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr, size_t index,
    size_t tag_index, char* name_buffer, size_t name_buffer_capacity, size_t* name_size,
    char* value_buffer, size_t value_buffer_capacity, size_t* value_size) {
  return getTag(toFlushContext(snapshot_envoy_ptr)->snapshot_.gauges(), index, tag_index,
                name_buffer, name_buffer_capacity, name_size, value_buffer, value_buffer_capacity,
                value_size);
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
  return getTagCount(toFlushContext(snapshot_envoy_ptr)->snapshot_.textReadouts(), index,
                     tag_count);
}

bool envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout_tag(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr, size_t index,
    size_t tag_index, char* name_buffer, size_t name_buffer_capacity, size_t* name_size,
    char* value_buffer, size_t value_buffer_capacity, size_t* value_size) {
  return getTag(toFlushContext(snapshot_envoy_ptr)->snapshot_.textReadouts(), index, tag_index,
                name_buffer, name_buffer_capacity, name_size, value_buffer, value_buffer_capacity,
                value_size);
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
