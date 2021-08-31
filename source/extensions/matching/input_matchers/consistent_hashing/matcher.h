#pragma once

#include "envoy/matcher/matcher.h"

#include "source/common/common/hash.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace ConsistentHashing {

class Matcher : public Envoy::Matcher::InputMatcher {
public:
  Matcher(uint32_t threshold, uint32_t modulo, uint64_t seed)
      : threshold_(threshold), modulo_(modulo), seed_(seed) {}
  bool match(absl::optional<absl::string_view> input) override {
    // Only match if the value is present.
    if (!input) {
      return false;
    }

    // Otherwise, match if (hash(input) % modulo) >= threshold.
    return HashUtil::xxHash64(*input, seed_) % modulo_ >= threshold_;
  }

private:
  const uint32_t threshold_;
  const uint32_t modulo_;
  const uint64_t seed_;
};
} // namespace ConsistentHashing
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
