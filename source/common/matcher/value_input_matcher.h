#pragma once

#include "envoy/matcher/matcher.h"

#include "source/common/common/matchers.h"

namespace Envoy {
namespace Matcher {

template <class StringMatcherType>
class StringInputMatcher : public InputMatcher, Logger::Loggable<Logger::Id::matcher> {
public:
  explicit StringInputMatcher(const StringMatcherType& matcher,
                              Server::Configuration::CommonFactoryContext& context)
      : matcher_(matcher, context) {}

  bool match(const MatchingDataType& input) override {
    if (absl::holds_alternative<std::string>(input)) {
      return matcher_.match(absl::get<std::string>(input));
    }
    // Return false when input is empty.(i.e., input is absl::monostate).
    return false;
  }

private:
  const Matchers::StringMatcherImpl<StringMatcherType> matcher_;
};

} // namespace Matcher
} // namespace Envoy
