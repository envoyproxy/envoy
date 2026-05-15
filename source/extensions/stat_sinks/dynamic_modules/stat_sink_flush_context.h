#pragma once

#include <string>
#include <vector>

#include "envoy/stats/sink.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace DynamicModules {

/**
 * Context passed to a dynamic module during a stats sink flush.
 */
struct StatSinkFlushContext {
  Stats::MetricSnapshot* snapshot;
  std::vector<std::string> counter_names;
  std::vector<std::string> gauge_names;
  std::vector<std::string> text_readout_names;
  std::vector<std::string> text_readout_values;
};

} // namespace DynamicModules
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
