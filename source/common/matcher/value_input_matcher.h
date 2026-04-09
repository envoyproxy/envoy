#pragma once

#include "envoy/matcher/matcher.h"

#include "source/common/common/matchers.h"

namespace Envoy {
namespace Matcher {

class StringInputMatcher : public InputMatcher, Logger::Loggable<Logger::Id::matcher> {
public:
  template <class StringMatcherType>
  explicit StringInputMatcher(const StringMatcherType& matcher,
                              Server::Configuration::CommonFactoryContext& context)
      : matcher_(matcher, context) {}

  MatchResult match(const DataInputGetResult& input) override {
    const auto data = input.stringData();
    if (data && matcher_.match(*data)) {
      return MatchResult::Matched;
    }
    // Return false when input is empty.(i.e., input is absl::monostate).
    return MatchResult::NoMatch;
  }

private:
  const Matchers::StringMatcherImpl matcher_;
};

} // namespace Matcher
} // namespace Envoy
