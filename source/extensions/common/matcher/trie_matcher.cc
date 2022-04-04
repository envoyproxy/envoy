#include "source/extensions/common/matcher/trie_matcher.h"

#include "envoy/http/filter.h"
#include "envoy/network/filter.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Matcher {

class NetworkTrieMatcherFactory : public TrieMatcherFactoryBase<Network::MatchingData> {};
class UdpNetworkTrieMatcherFactory : public TrieMatcherFactoryBase<Network::UdpMatchingData> {};
class HttpTrieMatcherFactory : public TrieMatcherFactoryBase<Http::HttpMatchingData> {};

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
