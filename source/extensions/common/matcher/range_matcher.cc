#include "source/extensions/common/matcher/range_matcher.h"

#include "envoy/http/filter.h"
#include "envoy/network/filter.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Matcher {

class NetworkRangeMatcherFactory : public RangeMatcherFactoryBase<Network::MatchingData> {};

REGISTER_FACTORY(NetworkRangeMatcherFactory,
                 ::Envoy::Matcher::CustomMatcherFactory<Network::MatchingData>);

class HttpRangeMatcherFactory : public RangeMatcherFactoryBase<Http::HttpMatchingData> {};

REGISTER_FACTORY(HttpRangeMatcherFactory,
                 ::Envoy::Matcher::CustomMatcherFactory<Http::HttpMatchingData>);

} // namespace Matcher
} // namespace Common
} // namespace Extensions
} // namespace Envoy
