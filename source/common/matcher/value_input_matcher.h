#pragma once

#include "envoy/matcher/matcher.h"

#include "source/common/common/matchers.h"

namespace Envoy {
namespace Matcher {

template <class StringMatcherType> class StringInputMatcher : public InputMatcher {
public:
  explicit StringInputMatcher(const StringMatcherType& matcher) : matcher_(matcher) {}

  bool match(const InputValue& input) override {
    switch (input.kind()) {
    case InputValue::Kind::Null:
      return false;
    case InputValue::Kind::List: {
      for (const auto& elt : input.asList()) {
        if (match(elt)) {
          return true;
        }
      }
      return false;
    }
    case InputValue::Kind::String:
      return matcher_.match(input.asString());
    case InputValue::Kind::Int:
      return matcher_.match(absl::StrCat(input.asInt()));
    }
  }

private:
  const Matchers::StringMatcherImpl<StringMatcherType> matcher_;
};

} // namespace Matcher
} // namespace Envoy
