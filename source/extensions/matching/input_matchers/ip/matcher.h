#pragma once

#include <vector>

#include "envoy/matcher/matcher.h"
#include "envoy/network/address.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/network/lc_trie.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace IP {

#define IP_MATCHER_STATS(COUNTER) COUNTER(ip_parsing_failed)

struct MatcherStats {
  IP_MATCHER_STATS(GENERATE_COUNTER_STRUCT);
};

class Matcher : public Envoy::Matcher::InputMatcher, Logger::Loggable<Logger::Id::filter> {
public:
  Matcher(std::vector<Network::Address::CidrRange>&& ranges, absl::string_view stat_prefix,
          Stats::Scope& stat_scope);
  bool match(absl::optional<absl::string_view> input) override;
  absl::optional<const MatcherStats> stats() const { return stats_; }

private:
  MatcherStats generateStats(absl::string_view prefix, Stats::Scope& scope);

  const Network::LcTrie::LcTrie<bool> trie_;
  absl::optional<MatcherStats> stats_;
};

} // namespace IP
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
