// NOLINT(namespace-envoy)

#include <string>

#include "envoy/stats/sink.h"

#include "source/extensions/dynamic_modules/abi/abi.h"

namespace {

Envoy::Stats::MetricSnapshot*
toSnapshot(envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr ptr) {
  return static_cast<Envoy::Stats::MetricSnapshot*>(ptr);
}

// Flush is single-threaded, so a thread_local string is safe to hold the name/value
// returned by-value from stat accessors. The pointer remains valid until the next call.
thread_local std::string tls_name;
thread_local std::string tls_value;

} // namespace

extern "C" {

size_t envoy_dynamic_module_callback_stat_sink_snapshot_get_counter_count(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr) {
  return toSnapshot(snapshot_envoy_ptr)->counters().size();
}

bool envoy_dynamic_module_callback_stat_sink_snapshot_get_counter(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr, size_t index,
    envoy_dynamic_module_type_envoy_buffer* name_out, uint64_t* value_out, uint64_t* delta_out) {
  const auto& counters = toSnapshot(snapshot_envoy_ptr)->counters();
  if (index >= counters.size()) {
    return false;
  }
  const auto& snap = counters[index];
  tls_name = snap.counter_.get().name();
  *name_out = {.ptr = tls_name.data(), .length = tls_name.size()};
  *value_out = snap.counter_.get().value();
  *delta_out = snap.delta_;
  return true;
}

size_t envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge_count(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr) {
  return toSnapshot(snapshot_envoy_ptr)->gauges().size();
}

bool envoy_dynamic_module_callback_stat_sink_snapshot_get_gauge(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr, size_t index,
    envoy_dynamic_module_type_envoy_buffer* name_out, uint64_t* value_out) {
  const auto& gauges = toSnapshot(snapshot_envoy_ptr)->gauges();
  if (index >= gauges.size()) {
    return false;
  }
  const Envoy::Stats::Gauge& gauge = gauges[index].get();
  tls_name = gauge.name();
  *name_out = {.ptr = tls_name.data(), .length = tls_name.size()};
  *value_out = gauge.value();
  return true;
}

size_t envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout_count(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr) {
  return toSnapshot(snapshot_envoy_ptr)->textReadouts().size();
}

bool envoy_dynamic_module_callback_stat_sink_snapshot_get_text_readout(
    envoy_dynamic_module_type_stat_sink_snapshot_envoy_ptr snapshot_envoy_ptr, size_t index,
    envoy_dynamic_module_type_envoy_buffer* name_out,
    envoy_dynamic_module_type_envoy_buffer* value_out) {
  const auto& readouts = toSnapshot(snapshot_envoy_ptr)->textReadouts();
  if (index >= readouts.size()) {
    return false;
  }
  tls_name = readouts[index].get().name();
  tls_value = readouts[index].get().value();
  *name_out = {.ptr = tls_name.data(), .length = tls_name.size()};
  *value_out = {.ptr = tls_value.data(), .length = tls_value.size()};
  return true;
}

} // extern "C"
