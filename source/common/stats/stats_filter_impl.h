#pragma once

#include <string>

#include "envoy/config/metrics/v2/stats.pb.h"
#include "envoy/stats/stats_filter.h"

#include "common/common/matchers.h"
#include "common/protobuf/protobuf.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

/**
 * Supplies a stats filter.
 */
class StatsFilterImpl : public StatsFilter {
public:
  StatsFilterImpl(const envoy::config::metrics::v2::StatsFilter& filter);

  // Default constructor simply allows everything.
  StatsFilterImpl() {}

  /**
   * Take a metric name and report whether or not it should be disallowed.
   * @param name std::string& a name of Stats::Metric (Counter, Gauge, Histogram).
   * @return true if that stat should not disallowed.
   */
  bool rejects(const std::string& name) const override;

private:
  // We want to allow stats through by default.
  bool default_inclusive_ = true;

  std::vector<Matchers::StringMatcher> matchers_;
};

} // namespace Stats
} // namespace Envoy
