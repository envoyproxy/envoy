#pragma once

#include "envoy/matcher/matcher.h"
#include "rust/cxx.h"
#include "source/extensions/matching/input_matchers/consistent_hashing/matcher.rs.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace ConsistentHashing {

class Matcher : public Envoy::Matcher::InputMatcher {
public:
  Matcher(uint32_t threshold, uint32_t modulo, uint64_t seed)
      : matcher_(newMatcher(threshold, modulo, seed)) {}
  bool match(absl::optional<absl::string_view> input) override {
    // Only match if the value is present.
    // TODO(snowp): Figure out how to model optional values via Rust.
    if (!input) {
      return false;
    }

    // Otherwise, match if (hash(input) % modulo) >= threshold.
    return doMatch(*matcher_, {input->data(), input->length()});
  }

private:
  rust::Box<RustMatcher> matcher_;
};
} // namespace ConsistentHashing
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
