#pragma once

#include "envoy/matcher/matcher.h"

#include "source/common/common/matchers.h"

namespace Envoy {
namespace Matcher {

template <class StringMatcherType> class StringInputMatcher : public InputMatcher {
public:
  explicit StringInputMatcher(const StringMatcherType& matcher) : matcher_(matcher) {}

  bool match(absl::optional<absl::string_view> input) override {
    if (!input) {
      return false;
    }

    return matcher_.match(*input);
  }

private:
  const Matchers::StringMatcherImpl<StringMatcherType> matcher_;
};

} // namespace Matcher
} // namespace Envoy
