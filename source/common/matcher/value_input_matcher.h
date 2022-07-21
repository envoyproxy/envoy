#pragma once

#include "envoy/matcher/matcher.h"

#include "source/common/common/matchers.h"

namespace Envoy {
namespace Matcher {

template <class StringMatcherType> class StringInputMatcher : public InputMatcher {
public:
  explicit StringInputMatcher(const StringMatcherType& matcher) : matcher_(matcher) {}

  bool match(const InputValue& input) override {
    const auto string_input = input.stringOrInt();
    if (!string_input) {
      return false;
    }
    return matcher_.match(*string_input);
  }

private:
  const Matchers::StringMatcherImpl<StringMatcherType> matcher_;
};

} // namespace Matcher
} // namespace Envoy
