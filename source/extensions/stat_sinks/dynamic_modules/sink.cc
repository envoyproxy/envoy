#include "source/extensions/stat_sinks/dynamic_modules/sink.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace DynamicModules {

DynamicModuleStatsSink::DynamicModuleStatsSink(DynamicModuleStatsSinkConfigSharedPtr config)
    : config_(std::move(config)) {}

void DynamicModuleStatsSink::flush(Stats::MetricSnapshot& snapshot) {
  config_->on_flush_(config_->in_module_config_, thisAsVoidPtr(), static_cast<void*>(&snapshot));
}

void DynamicModuleStatsSink::onHistogramComplete(const Stats::Histogram& histogram,
                                                 uint64_t value) {
  const std::string name = histogram.name();
  envoy_dynamic_module_type_envoy_buffer name_buf = {.ptr = name.data(), .length = name.size()};
  config_->on_histogram_complete_(config_->in_module_config_, name_buf, value);
}

} // namespace DynamicModules
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
