// NOLINT(namespace-envoy)

#include <string>

#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/stat_sinks/dynamic_modules/flush_context.h"

namespace {

using Envoy::Extensions::StatSinks::DynamicModules::DynamicModuleStatsSinkFlushContext;

DynamicModuleStatsSinkFlushContext*
toFlushContext(envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr ptr) {
  return static_cast<DynamicModuleStatsSinkFlushContext*>(ptr);
}

} // namespace

extern "C" {

size_t envoy_dynamic_module_callback_stat_sink_snapshot_get_counter_count(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr) {
  return toFlushContext(snapshot_envoy_ptr)->snapshot_.counters().size();
}

bool envoy_dynamic_module_callback_stat_sink_snapshot_get_counter(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr, size_t index,
    envoy_dynamic_module_type_envoy_buffer* name_out, uint64_t* value_out, uint64_t* delta_out) {
  auto* context = toFlushContext(snapshot_envoy_ptr);
  const auto& counters = context->snapshot_.counters();
  if (index >= counters.size()) {
    return false;
  }
  const auto& snap = counters[index];
  const Envoy::Stats::Counter& counter = snap.counter_.get();
  const std::string& name = context->string_storage_.emplace_back(counter.name());
  *name_out = {.ptr = name.data(), .length = name.size()};
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
    envoy_dynamic_module_type_envoy_buffer* name_out, uint64_t* value_out) {
  auto* context = toFlushContext(snapshot_envoy_ptr);
  const auto& gauges = context->snapshot_.gauges();
  if (index >= gauges.size()) {
    return false;
  }
  const Envoy::Stats::Gauge& gauge = gauges[index].get();
  const std::string& name = context->string_storage_.emplace_back(gauge.name());
  *name_out = {.ptr = name.data(), .length = name.size()};
  *value_out = gauge.value();
  return true;
}

size_t envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout_count(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr) {
  return toFlushContext(snapshot_envoy_ptr)->snapshot_.textReadouts().size();
}

bool envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr, size_t index,
    envoy_dynamic_module_type_envoy_buffer* name_out,
    envoy_dynamic_module_type_envoy_buffer* value_out) {
  auto* context = toFlushContext(snapshot_envoy_ptr);
  const auto& readouts = context->snapshot_.textReadouts();
  if (index >= readouts.size()) {
    return false;
  }
  const Envoy::Stats::TextReadout& readout = readouts[index].get();
  const std::string& name = context->string_storage_.emplace_back(readout.name());
  const std::string& value = context->string_storage_.emplace_back(readout.value());
  *name_out = {.ptr = name.data(), .length = name.size()};
  *value_out = {.ptr = value.data(), .length = value.size()};
  return true;
}

} // extern "C"
