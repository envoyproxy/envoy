#pragma once

#include "source/common/common/hash.h"

#include "rust/cxx.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace ConsistentHashing {

/**
 * Simplified API allowing Rust to call into xxHash64.
 */
uint64_t hash(rust::Str value, uint64_t seed);

} // namespace ConsistentHashing
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
