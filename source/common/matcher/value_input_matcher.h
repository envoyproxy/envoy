#pragma once

#include "envoy/matcher/matcher.h"

#include "common/common/matchers.h"

namespace Envoy {
namespace Matcher {

class StringInputMatcher : public InputMatcher {
public:
  explicit StringInputMatcher(const envoy::type::matcher::v3::StringMatcher& matcher)
      : matcher_(matcher) {}

  bool match(absl::optional<absl::string_view> input) override {
    if (!input) {
      return false;
    }

    return matcher_.match(*input);
  }

private:
  const Matchers::StringMatcherImpl matcher_;
};

} // namespace Matcher
} // namespace Envoy