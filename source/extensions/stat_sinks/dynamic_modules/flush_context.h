#pragma once

#include <deque>
#include <string>

#include "envoy/stats/sink.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace DynamicModules {

/**
 * Per-flush snapshot handle passed to the module as the opaque snapshot pointer.
 *
 * It owns the stat names and values materialized by the snapshot callbacks so their buffers stay
 * valid until the flush hook returns. A std::deque is used because element addresses remain stable
 * as strings are appended.
 */
struct DynamicModuleStatsSinkFlushContext {
  explicit DynamicModuleStatsSinkFlushContext(Stats::MetricSnapshot& snapshot)
      : snapshot_(snapshot) {}

  Stats::MetricSnapshot& snapshot_;
  std::deque<std::string> string_storage_;
};

} // namespace DynamicModules
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
