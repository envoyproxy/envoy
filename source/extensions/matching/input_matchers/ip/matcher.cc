#include "source/extensions/matching/input_matchers/ip/matcher.h"

#include "source/common/network/utility.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace IP {

namespace {

MatcherStats generateStats(absl::string_view prefix, Stats::Scope& scope) {
  return MatcherStats{IP_MATCHER_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
}

} // namespace

Matcher::Matcher(std::vector<Network::Address::CidrRange> const& ranges,
                 absl::string_view stat_prefix,
                 Stats::Scope& stat_scope)
    : // We could put "false" instead of "true". What matters is that the IP
      // belongs to the trie. We could further optimize the storage of LcTrie in
      // this case by implementing an LcTrie<void> specialization that doesn't
      // store any associated data.
      trie_({{true, ranges}}), stats_(generateStats(stat_prefix, stat_scope)) {}

bool Matcher::match(const Envoy::Matcher::MatchingDataType& input) {
  if (absl::holds_alternative<absl::monostate>(input)) {
    return false;
  }
  const std::string& ip_str = absl::get<std::string>(input);
  if (ip_str.empty()) {
    return false;
  }
  const auto ip = Network::Utility::parseInternetAddressNoThrow(ip_str);
  if (!ip) {
    stats_.ip_parsing_failed_.inc();
    ENVOY_LOG(debug, "IP matcher: unable to parse address '{}'", ip_str);
    return false;
  }
  return !trie_.getData(ip).empty();
}

} // namespace IP
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
