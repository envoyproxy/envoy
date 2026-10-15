#pragma once

#include "envoy/stats/sink.h"

#include "source/extensions/stat_sinks/dynamic_modules/sink_config.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace DynamicModules {

/**
 * Stats sink that delegates to a dynamic module. The config is shared across all sink instances
 * and the sink holds no per-instance state beyond the config pointer.
 */
class DynamicModuleStatsSink : public Stats::Sink {
public:
  explicit DynamicModuleStatsSink(DynamicModuleStatsSinkConfigSharedPtr config);

  // Stats::Sink
  void flush(Stats::MetricSnapshot& snapshot) override;
  void onHistogramComplete(const Stats::Histogram& histogram, uint64_t value) override;

private:
  DynamicModuleStatsSinkConfigSharedPtr config_;
};

} // namespace DynamicModules
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
