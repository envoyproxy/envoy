#include "source/extensions/stat_sinks/dynamic_modules/sink.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace DynamicModules {

DynamicModuleStatsSink::DynamicModuleStatsSink(DynamicModuleStatsSinkConfigSharedPtr config)
    : config_(std::move(config)) {}

void DynamicModuleStatsSink::flush(Stats::MetricSnapshot& snapshot) {
  // Metric::name() and TextReadout::value() both return std::string by value, so
  // materialize them once here and hand the module stable pointers via the context.
  StatSinkFlushContext ctx;
  ctx.snapshot = &snapshot;

  const auto& counters = snapshot.counters();
  ctx.counter_names.reserve(counters.size());
  for (const auto& counter_snap : counters) {
    ctx.counter_names.push_back(counter_snap.counter_.get().name());
  }

  const auto& gauges = snapshot.gauges();
  ctx.gauge_names.reserve(gauges.size());
  for (const auto& gauge_ref : gauges) {
    ctx.gauge_names.push_back(gauge_ref.get().name());
  }

  const auto& text_readouts = snapshot.textReadouts();
  ctx.text_readout_names.reserve(text_readouts.size());
  ctx.text_readout_values.reserve(text_readouts.size());
  for (const auto& readout_ref : text_readouts) {
    ctx.text_readout_names.push_back(readout_ref.get().name());
    ctx.text_readout_values.push_back(readout_ref.get().value());
  }

  config_->on_flush_(config_->in_module_config_, thisAsVoidPtr(), static_cast<void*>(&ctx));
}

void DynamicModuleStatsSink::onHistogramComplete(const Stats::Histogram& histogram,
                                                 uint64_t value) {
  // Metric::name() returns std::string by value; bind it to a local so the
  // buffer pointer stays valid for the duration of the module call.
  const std::string name = histogram.name();
  envoy_dynamic_module_type_envoy_buffer name_buf = {.ptr = name.data(), .length = name.size()};
  config_->on_histogram_complete_(config_->in_module_config_, name_buf, value);
}

} // namespace DynamicModules
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
