#pragma once

#include <string>

#include "envoy/config/metrics/v2/stats.pb.h"
#include "envoy/stats/stats_matcher.h"

#include "common/common/matchers.h"
#include "common/protobuf/protobuf.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

/**
 * Supplies a stats matcher.
 */
class StatsMatcherImpl : public StatsMatcher {
public:
  StatsMatcherImpl(const envoy::config::metrics::v2::StatsConfig& config);

  // Default constructor simply allows everything.
  StatsMatcherImpl() : is_inclusive_(true) {}

  /**
   * Take a metric name and report whether or not it should be disallowed.
   * @param name std::string& a name of Stats::Metric (Counter, Gauge, Histogram).
   * @return true if that stat should not disallowed.
   */
  bool rejects(const std::string& name) const override;

private:
  // We want to allow stats through by default.
  bool is_inclusive_;

  std::vector<Matchers::StringMatcher> matchers_;
};

} // namespace Stats
} // namespace Envoy
