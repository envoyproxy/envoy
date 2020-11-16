#pragma once

#include "envoy/matcher/matcher.h"

#include "common/common/matchers.h"

namespace Envoy {
namespace Matcher {

class ValueInputMatcher : public InputMatcher {
public:
  explicit ValueInputMatcher(const envoy::type::matcher::v3::ValueMatcher& matcher)
      : matcher_(Matchers::ValueMatcher::create(matcher)) {}

  bool match(absl::optional<absl::string_view> input) override {
    if (!input) {
      // TODO(snowp): This should match against the null matcher.
      return false;
    }
    // TODO(snowp): Change this to properly match against the input and not go through a proto
    // matcher.
    ProtobufWkt::Value value;
    value.set_string_value(std::string(*input));
    const auto r = matcher_->match(value);

    return r;
  }

private:
  const Matchers::ValueMatcherConstSharedPtr matcher_;
};

} // namespace Matcher
} // namespace Envoy