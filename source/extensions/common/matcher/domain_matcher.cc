#include "source/extensions/common/matcher/domain_matcher.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Matcher {

class NetworkTrieMatcherFactory : public TrieMatcherFactoryBase<Network::MatchingData> {};
class UdpNetworkTrieMatcherFactory : public TrieMatcherFactoryBase<Network::UdpMatchingData> {};

REGISTER_FACTORY(TcpServerNameMatcherFactory,
                 ::Envoy::Matcher::CustomMatcherFactory<Network::MatchingData>);
//REGISTER_FACTORY(HttpServerNameMatcherFactory,
//                 ::Envoy::Matcher::CustomMatcherFactory<Http::HttpMatchingData>);

} // namespace Matcher
} // namespace Common
} // namespace Extensions
} // namespace Envoy
