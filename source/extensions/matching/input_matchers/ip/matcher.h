#pragma once

#include <vector>

#include "envoy/matcher/matcher.h"
#include "envoy/network/address.h"

#include "common/network/lc_trie.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace IP {

class Matcher : public Envoy::Matcher::InputMatcher {
public:
  Matcher(std::vector<Network::Address::CidrRange>&& ranges);
  bool match(absl::optional<absl::string_view> input) override;

private:
  const Network::LcTrie::LcTrie<bool> trie_;
};

} // namespace IP
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
