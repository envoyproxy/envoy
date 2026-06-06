// NOLINT(namespace-envoy)

#include <algorithm>
#include <cstring>

#include "source/common/common/assert.h"
#include "source/common/stats/symbol_table.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/stat_sinks/dynamic_modules/flush_context.h"

#include "absl/strings/string_view.h"

namespace {

using Envoy::Extensions::StatSinks::DynamicModules::DynamicModuleStatsSinkFlushContext;

DynamicModuleStatsSinkFlushContext*
toFlushContext(envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr ptr) {
  return static_cast<DynamicModuleStatsSinkFlushContext*>(ptr);
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

} // extern "C"
