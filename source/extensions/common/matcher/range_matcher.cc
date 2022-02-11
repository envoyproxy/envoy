#include "source/extensions/common/matcher/range_matcher.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Matcher {

REGISTER_FACTORY(NetworkRangeMatcherFactory,
                 ::Envoy::Matcher::CustomMatcherFactory<Network::MatchingData>);

} // namespace Matcher
} // namespace Common
} // namespace Extensions
} // namespace Envoy
