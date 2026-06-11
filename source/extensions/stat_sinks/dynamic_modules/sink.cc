#include "source/extensions/stat_sinks/dynamic_modules/sink.h"

#include "source/common/common/assert.h"
#include "source/extensions/stat_sinks/dynamic_modules/flush_context.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace DynamicModules {

DynamicModuleStatsSink::DynamicModuleStatsSink(DynamicModuleStatsSinkConfigSharedPtr config)
    : config_(std::move(config)) {}

void DynamicModuleStatsSink::flush(Stats::MetricSnapshot& snapshot) {
  ASSERT(config_->on_flush_ != nullptr);
  DynamicModuleStatsSinkFlushContext context(snapshot);
  config_->on_flush_(config_->in_module_config_, &context);
}

void DynamicModuleStatsSink::onHistogramComplete(const Stats::Histogram& histogram,
                                                 uint64_t value) {
  // The config members are written once during config creation on the main thread
  // before any worker thread starts, so reading them here needs no synchronization.
  ASSERT(config_->on_histogram_complete_ != nullptr);
  const std::string name = histogram.name();
  envoy_dynamic_module_type_envoy_buffer name_buf = {.ptr = name.data(), .length = name.size()};
  config_->on_histogram_complete_(config_->in_module_config_, name_buf, value);
}

} // namespace DynamicModules
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
