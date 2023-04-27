#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/matcher/matcher.h"
#include "envoy/runtime/runtime.h"

#include "source/common/common/hash.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace RuntimeFraction {

class Matcher : public Envoy::Matcher::InputMatcher {
public:
  Matcher(Runtime::Loader& runtime,
          envoy::config::core::v3::RuntimeFractionalPercent runtime_fraction, uint64_t seed)
      : runtime_(runtime), runtime_fraction_(runtime_fraction), seed_(seed) {}
  bool match(absl::optional<absl::string_view> input) override {
    // Only match if the value is present.
    if (!input) {
      return false;
    }

    // Otherwise, match if feature is enabled for hash(input).
    const auto hash_value = HashUtil::xxHash64(*input, seed_);
    return runtime_.snapshot().featureEnabled(runtime_fraction_.runtime_key(),
                                              runtime_fraction_.default_value(), hash_value);
  }

private:
  Runtime::Loader& runtime_;
  const envoy::config::core::v3::RuntimeFractionalPercent runtime_fraction_;
  const uint64_t seed_;
};
} // namespace RuntimeFraction
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
