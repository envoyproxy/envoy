#pragma once

#include <memory>

#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Upstream {

/**
 * All load reporter stats. @see stats_macros.h
 */
#define ALL_LOAD_REPORTER_STATS(COUNTER)                                                           \
  COUNTER(requests)                                                                                \
  COUNTER(responses)                                                                               \
  COUNTER(errors)                                                                                  \
  COUNTER(retries)

/**
 * Struct definition for all load reporter stats. @see stats_macros.h
 */
struct LoadReporterStats {
  ALL_LOAD_REPORTER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Interface for load stats reporting.
 */
class LoadStatsReporter {
public:
  virtual ~LoadStatsReporter() = default;

  /**
   * @return the load reporter stats.
   */
  virtual const LoadReporterStats& getStats() const PURE;
};

using LoadStatsReporterPtr = std::unique_ptr<LoadStatsReporter>;

} // namespace Upstream
} // namespace Envoy
