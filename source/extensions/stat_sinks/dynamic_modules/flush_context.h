#pragma once

#include <vector>

#include "envoy/stats/sink.h"
#include "envoy/stats/stats.h"

#include "source/common/stats/symbol_table.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace DynamicModules {

/**
 * Per-flush snapshot handle passed to the module as the opaque snapshot pointer.
 *
 * The snapshot callbacks decode stat names directly into module-provided buffers. A small tag cache
 * `memoizes` the last metric's tags so a full per-metric tag read stays linear. This only borrows
 * the snapshot for the duration of the flush hook.
 */
struct DynamicModuleStatsSinkFlushContext {
  explicit DynamicModuleStatsSinkFlushContext(Stats::MetricSnapshot& snapshot)
      : snapshot_(snapshot) {}

  Stats::MetricSnapshot& snapshot_;

  // Caches the decoded tag StatNames of the most recently read metric. A module reads a metric's
  // tags in order, so `memoizing` the last metric collapses the per-index tag walks from O(T^2) to
  // O(T) for a full read. The StatNames borrow the metric and stay valid for the flush.
  const Stats::Metric* cached_metric_ = nullptr;
  std::vector<Stats::StatNameTag> cached_tags_;
};

} // namespace DynamicModules
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
