#include "source/extensions/common/matcher/trie_matcher.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Matcher {

REGISTER_FACTORY(NetworkTrieMatcherFactory,
                 ::Envoy::Matcher::CustomMatcherFactory<Network::MatchingData>);
REGISTER_FACTORY(UdpNetworkTrieMatcherFactory,
                 ::Envoy::Matcher::CustomMatcherFactory<Network::UdpMatchingData>);
REGISTER_FACTORY(HttpTrieMatcherFactory,
                 ::Envoy::Matcher::CustomMatcherFactory<Http::HttpMatchingData>);

} // namespace Matcher
} // namespace Common
} // namespace Extensions
} // namespace Envoy
