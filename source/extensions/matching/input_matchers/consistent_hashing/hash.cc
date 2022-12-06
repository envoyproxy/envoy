#include "source/extensions/matching/input_matchers/consistent_hashing/hash.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace ConsistentHashing {
uint64_t hash(rust::Str value, uint64_t seed) {
  return Envoy::HashUtil::xxHash64({value.data(), value.length()}, seed);
}
} // namespace ConsistentHashing
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
