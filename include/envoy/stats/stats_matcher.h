#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/pure.h"

namespace Envoy {
namespace Stats {

class StatsMatcher {
public:
  virtual ~StatsMatcher() {}

  /**
   * Take a metric name and report whether or not it should be instantiated.
   * @param the name of a Stats::Metric.
   * @return bool true if that stat should not be instantiated.
   */
  virtual bool rejects(const std::string& name) const PURE;
};

typedef std::unique_ptr<const StatsMatcher> StatsMatcherPtr;

} // namespace Stats
} // namespace Envoy
