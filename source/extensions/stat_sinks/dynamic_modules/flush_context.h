#pragma once

#include "envoy/stats/sink.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace DynamicModules {

/**
 * Per-flush snapshot handle passed to the module as the opaque snapshot pointer.
 *
 * The snapshot callbacks decode stat names directly into module-provided buffers, so no Envoy-side
 * storage is needed. This only borrows the snapshot for the duration of the flush hook.
 */
struct DynamicModuleStatsSinkFlushContext {
  explicit DynamicModuleStatsSinkFlushContext(Stats::MetricSnapshot& snapshot)
      : snapshot_(snapshot) {}

  Stats::MetricSnapshot& snapshot_;
};

} // namespace DynamicModules
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
