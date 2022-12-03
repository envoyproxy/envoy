#pragma once

#include "envoy/matcher/matcher.h"
#include "rust/cxx.h"
#include "source/extensions/matching/input_matchers/rust_re/matcher.rs.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace RustRe {

class Matcher : public Envoy::Matcher::InputMatcher {
public:
  Matcher(absl::string_view regex) : matcher_(newReMatcher({regex.data(), regex.length()})) {}
  bool match(absl::optional<absl::string_view> input) override {
    // Only match if the value is present.
    if (input) {
      return doMatch(*matcher_, {input->data(), input->length()});
    }

    return false;
  }

  rust::Box<ReMatcher> matcher_;
};
} // namespace RustRe
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
