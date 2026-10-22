#pragma once

#include <memory>

#include "envoy/matcher/matcher.h"

#include "source/common/http/path_utility.h"

#include "re2/re2.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace UriTemplate {

class UriTemplateInputMatcher : public Envoy::Matcher::InputMatcher {
public:
  explicit UriTemplateInputMatcher(std::shared_ptr<const RE2> regex) : regex_(std::move(regex)) {}

  Envoy::Matcher::MatchResult match(const Envoy::Matcher::DataInputGetResult& input) override {
    const auto path = input.stringData();
    if (path && RE2::FullMatch(Envoy::Http::PathUtil::removeQueryAndFragment(*path), *regex_)) {
      return Envoy::Matcher::MatchResult::Matched;
    }
    return Envoy::Matcher::MatchResult::NoMatch;
  }

private:
  const std::shared_ptr<const RE2> regex_;
};

} // namespace UriTemplate
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
