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
  bool match(const Envoy::Matcher::MatchingDataType& input) override {
    // Only match if the value is present.
    if (absl::holds_alternative<absl::monostate>(input)) {
      return false;
    }

    // Otherwise, match if (hash(input) % modulo) >= threshold.
    return HashUtil::xxHash64(absl::get<std::string>(input), seed_) % modulo_ >= threshold_;
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
