#pragma once

#include "envoy/matcher/matcher.h"
#include "envoy/router/path_matcher.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace UriTemplate {

class UriTemplateInputMatcher : public Envoy::Matcher::InputMatcher {
public:
  explicit UriTemplateInputMatcher(Router::PathMatcherSharedPtr path_matcher)
      : path_matcher_(std::move(path_matcher)) {}

  Envoy::Matcher::MatchResult match(const Envoy::Matcher::DataInputGetResult& input) override {
    const auto path = input.stringData();
    if (path && path_matcher_->match(*path)) {
      return Envoy::Matcher::MatchResult::Matched;
    }
    return Envoy::Matcher::MatchResult::NoMatch;
  }

private:
  const Router::PathMatcherSharedPtr path_matcher_;
};

} // namespace UriTemplate
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
