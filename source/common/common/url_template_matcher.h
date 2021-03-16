#pragma once

#include "envoy/common/matchers.h"

#include "include/http_pattern_matcher/path_matcher.h"

namespace Envoy {
namespace Matchers {

class UrlTemplateMatcher : public StringMatcher {
public:
  UrlTemplateMatcher(const absl::string_view url_template);

  bool match(const absl::string_view path) const override;

private:
  google::http::pattern_matcher::PathMatcherPtr<const UrlTemplateMatcher*> path_matcher_;
};

using UrlTemplateMatcherConstPtr = std::unique_ptr<const UrlTemplateMatcher>;

} // namespace Matchers
} // namespace Envoy
