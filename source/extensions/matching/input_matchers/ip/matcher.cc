#include "extensions/matching/input_matchers/ip/matcher.h"

#include "common/network/utility.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace IP {

Matcher::Matcher(std::vector<Network::Address::CidrRange>&& ranges)
    // We could put "false" instead of "true". What matters is that the IP
    // belongs to the trie. We could further optimize the storage of LcTrie in
    // this case by implementing an LcTrie<void> specialization that doesn't
    // store any associated data.
    : trie_({{true, std::move(ranges)}}) {}

bool Matcher::match(absl::optional<absl::string_view> input) {
  if (!input) {
    return false;
  }
  const absl::string_view& ip_str = *input;
  if (ip_str.empty()) {
    return false;
  }
  const auto ip = Network::Utility::parseInternetAddress(std::string{ip_str});
  if (!ip) {
    return false;
  }
  return !trie_.getData(ip).empty();
}

} // namespace IP
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
