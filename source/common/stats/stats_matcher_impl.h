#pragma once

#include <string>

#include "envoy/config/metrics/v3/stats.pb.h"
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
  explicit StatsMatcherImpl(const envoy::config::metrics::v3::StatsConfig& config);

  // Default constructor simply allows everything.
  StatsMatcherImpl() = default;

  // StatsMatcher
  bool rejects(const std::string& name) const override;
  bool acceptsAll() const override { return is_inclusive_ && matchers_.empty(); }
  bool rejectsAll() const override { return !is_inclusive_ && matchers_.empty(); }

private:
  // Bool indicating whether or not the StatsMatcher is including or excluding stats by default. See
  // StatsMatcherImpl::rejects() for much more detail.
  bool is_inclusive_{true};

  std::vector<Matchers::StringMatcherImpl> matchers_;
};

} // namespace Stats
} // namespace Envoy
