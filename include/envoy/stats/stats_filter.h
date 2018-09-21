#pragma once

#include <string>
#include <vector>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Stats {

class StatsFilter {
public:
  virtual ~StatsFilter() {}

  /**
   * Take a metric name and report whether or not it should be instantiated.
   * @param name std::string& a name of Stats::Metric (Counter, Gauge, Histogram).
   * @return true if that stat should not be instantiated.
   */
  virtual bool rejects(const std::string& name) const PURE;
};

typedef std::unique_ptr<const StatsFilter> StatsFilterPtr;

} // namespace Stats
} // namespace Envoy
