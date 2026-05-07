#pragma once

#include "envoy/stats/sink.h"

#include "source/extensions/dynamic_modules/stat_sink_flush_context.h"
#include "source/extensions/stat_sinks/dynamic_modules/sink_config.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace DynamicModules {

// Re-exported for convenience; the struct itself lives in core so the ABI
// callbacks that consume it are provided by every Envoy binary.
using ::Envoy::Extensions::DynamicModules::StatSinkFlushContext;

/**
 * Stats sink that delegates to a dynamic module. A single config is shared across all
 * instances; the sink itself holds no per-instance state beyond the config pointer.
 */
class DynamicModuleStatsSink : public Stats::Sink {
public:
  explicit DynamicModuleStatsSink(DynamicModuleStatsSinkConfigSharedPtr config);

  // Stats::Sink
  void flush(Stats::MetricSnapshot& snapshot) override;
  void onHistogramComplete(const Stats::Histogram& histogram, uint64_t value) override;

  /**
   * Helper to obtain the raw this pointer for use as the sink_envoy_ptr ABI argument.
   */
  void* thisAsVoidPtr() { return static_cast<void*>(this); }

private:
  DynamicModuleStatsSinkConfigSharedPtr config_;
};

} // namespace DynamicModules
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
