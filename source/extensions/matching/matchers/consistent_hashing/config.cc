#include "extensions/matching/matchers/consistent_hashing/config.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Matchers {
namespace ConsistentHashing {

/**
 * Static registration for the consistent hashing matcher. @see RegisterFactory.
 */
REGISTER_FACTORY(ConsistentHashingConfig, Envoy::Matcher::InputMatcherFactory);

} // namespace ConsistentHashing
} // namespace Matchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy