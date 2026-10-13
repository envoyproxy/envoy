// Note: this should be run with --compilation_mode=opt, and would benefit from a
// quiescent system with disabled cstate power management.

#include "envoy/config/metrics/v3/stats.pb.h"
#include "envoy/type/matcher/v3/string.pb.h"

#include "source/common/stats/stats_matcher_impl.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"

namespace Envoy {
namespace Stats {

class StatsMatcherPerf {
public:
  StatsMatcherPerf() : pool_(symbol_table_) {}

  void initMatcher() {
    stats_matcher_impl_ =
        std::make_unique<StatsMatcherImpl>(stats_config_, symbol_table_, context_);
  }

  envoy::type::matcher::v3::StringMatcher* inclusionList() {
    return stats_config_.mutable_stats_matcher()->mutable_inclusion_list()->add_patterns();
  }
  envoy::type::matcher::v3::StringMatcher* exclusionList() {
    return stats_config_.mutable_stats_matcher()->mutable_exclusion_list()->add_patterns();
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  SymbolTableImpl symbol_table_;
  StatNamePool pool_;
  std::unique_ptr<StatsMatcherImpl> stats_matcher_impl_;

private:
  envoy::config::metrics::v3::StatsConfig stats_config_;
};

} // namespace Stats
} // namespace Envoy

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_Inclusion(benchmark::State& state) {
  // Create matcher.
  Envoy::Stats::StatsMatcherPerf context;
  context.inclusionList()->set_suffix("1");
  context.inclusionList()->set_prefix("2.");
  context.initMatcher();
  std::vector<Envoy::Stats::StatName> stat_names;
  stat_names.reserve(10);
  for (auto idx = 0; idx < 10; ++idx) {
    stat_names.push_back(context.pool_.add(absl::StrCat("2.", idx)));
  }

  for (auto _ : state) { // NOLINT
    for (auto idx = 0; idx < 1000; ++idx) {
      const Envoy::Stats::StatName stat_name = stat_names[idx % 10];
      const Envoy::Stats::StatsMatcher::FastResult fast_result =
          context.stats_matcher_impl_->fastRejects(stat_name);
      context.stats_matcher_impl_->slowRejects(fast_result, stat_name);
    }
  }
}
BENCHMARK(BM_Inclusion);

// NOLINTNEXTLINE(readability-identifier-naming)
static void BM_Exclusion(benchmark::State& state) {
  // Create matcher.
  Envoy::Stats::StatsMatcherPerf context;
  context.exclusionList()->set_suffix("1");
  context.exclusionList()->set_prefix("2.");
  context.initMatcher();
  std::vector<Envoy::Stats::StatName> stat_names;
  stat_names.reserve(10);
  for (auto idx = 0; idx < 10; ++idx) {
    stat_names.push_back(context.pool_.add(absl::StrCat("2.", idx)));
  }

  for (auto _ : state) { // NOLINT
    for (auto idx = 0; idx < 1000; ++idx) {
      const Envoy::Stats::StatName stat_name = stat_names[idx % 10];
      const Envoy::Stats::StatsMatcher::FastResult fast_result =
          context.stats_matcher_impl_->fastRejects(stat_name);
      context.stats_matcher_impl_->slowRejects(fast_result, stat_name);
    }
  }
}
BENCHMARK(BM_Exclusion);
