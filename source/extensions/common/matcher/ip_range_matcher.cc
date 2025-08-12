#include "source/extensions/common/matcher/ip_range_matcher.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Matcher {

REGISTER_FACTORY(NetworkIpRangeMatcherFactory,
                 ::Envoy::Matcher::CustomMatcherFactory<Network::MatchingData>);
REGISTER_FACTORY(UdpNetworkIpRangeMatcherFactory,
                 ::Envoy::Matcher::CustomMatcherFactory<Network::UdpMatchingData>);
REGISTER_FACTORY(HttpIpRangeMatcherFactory,
                 ::Envoy::Matcher::CustomMatcherFactory<Http::HttpMatchingData>);

} // namespace Matcher
} // namespace Common
} // namespace Extensions
} // namespace Envoy
