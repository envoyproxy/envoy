#include "contrib/hyperscan/matching/input_matchers/source/config.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace Hyperscan {

REGISTER_FACTORY(Config, Envoy::Matcher::InputMatcherFactory);

} // namespace Hyperscan
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
