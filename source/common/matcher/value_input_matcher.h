#pragma once

#include "envoy/matcher/matcher.h"

#include "source/common/common/matchers.h"

namespace Envoy {
namespace Matcher {

template <class StringMatcherType>
class StringInputMatcher : public InputMatcher, Logger::Loggable<Logger::Id::matcher> {
public:
  explicit StringInputMatcher(const StringMatcherType& matcher) : matcher_(matcher) {}

  bool match(const MatchingDataType& input) override {
    if (absl::holds_alternative<std::string>(input)) {
      return matcher_.match(absl::get<std::string>(input));
    }

    // TODO(tyxia) As generic matching API is supported, integer type input is allowed such as
    // `DestinationPortInput`. In the short term, we reuse StringInputMatcher to handle this.
    // Eventually, we should have IntInputMatcher for integer type input.
    if (absl::holds_alternative<uint32_t>(input)) {
      return matcher_.match(std::to_string(absl::get<uint32_t>(input)));
    }

    // Apart from the `int` and `string` types above, the `absl::monostate` is also an expected
    // int type that represents the empty input. Therefore, log an error message for any types other
    // than these three.
    if (!absl::holds_alternative<absl::monostate>(input)) {
      ENVOY_LOG(error, "Unsupported input type!");
    }

    return false;
  }

private:
  const Matchers::StringMatcherImpl<StringMatcherType> matcher_;
};

} // namespace Matcher
} // namespace Envoy
