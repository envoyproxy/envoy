#include "source/extensions/stat_sinks/dynamic_modules/sink.h"

#include <vector>

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
  thread_local std::vector<char> histogram_name_buffer;
  const auto& symbol_table = histogram.constSymbolTable();
  const auto stat_name = histogram.statName();
  // Serialize into the reused buffer and retry only when it must grow, so the common case walks the
  // symbol table once instead of once to size and once to fill.
  const size_t required_size = symbol_table.serializeToBuffer(
      stat_name, histogram_name_buffer.data(), histogram_name_buffer.size());
  if (required_size > histogram_name_buffer.size()) {
    histogram_name_buffer.resize(required_size);
    symbol_table.serializeToBuffer(stat_name, histogram_name_buffer.data(),
                                   histogram_name_buffer.size());
  }
  envoy_dynamic_module_type_envoy_buffer name_buf = {.ptr = histogram_name_buffer.data(),
                                                     .length = required_size};
  config_->on_histogram_complete_(config_->in_module_config_, name_buf, value);
}

} // namespace DynamicModules
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
