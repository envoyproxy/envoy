#pragma once

#include <string>

#include "envoy/common/optref.h"
#include "envoy/config/metrics/v3/stats.pb.h"
#include "envoy/stats/stats_matcher.h"

#include "source/common/common/matchers.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/stats/symbol_table.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

/**
 * Supplies a stats matcher.
 */
class StatsMatcherImpl : public StatsMatcher {
public:
  StatsMatcherImpl(const envoy::config::metrics::v3::StatsConfig& config, SymbolTable& symbol_table,
                   Server::Configuration::CommonFactoryContext& context);

  // Default constructor simply allows everything.
  StatsMatcherImpl() = default;

  // StatsMatcher
  bool rejects(StatName name) const override {
    FastResult fast_result = fastRejects(name);
    return fast_result == FastResult::Rejects || slowRejects(fast_result, name);
  }
  FastResult fastRejects(StatName name) const override;
  bool slowRejects(FastResult, StatName name) const override;
  bool acceptsAll() const override {
    return is_inclusive_ && matchers_.empty() && prefixes_.empty();
  }
  bool rejectsAll() const override {
    return !is_inclusive_ && matchers_.empty() && prefixes_.empty();
  }

private:
  void optimizeLastMatcher();
  bool fastRejectMatch(StatName name) const;
  bool slowRejectMatch(StatName name) const;

  // Bool indicating whether or not the StatsMatcher is including or excluding stats by default. See
  // StatsMatcherImpl::rejects() for much more detail.
  bool is_inclusive_{true};

  OptRef<SymbolTable> symbol_table_;
  std::unique_ptr<StatNamePool> stat_name_pool_;

  std::vector<Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>> matchers_;
  std::vector<StatName> prefixes_;
};

} // namespace Stats
} // namespace Envoy
