#pragma once

#include <string>

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/type/matcher/metadata.pb.h"
#include "envoy/type/matcher/number.pb.h"
#include "envoy/type/matcher/string.pb.h"

#include "common/common/utility.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Matchers {

class DoubleMatcher {
public:
  DoubleMatcher(const envoy::type::matcher::DoubleMatcher& matcher) : matcher_(matcher) {}

  /**
   * Check whether the value is matched to the matcher.
   * @param value the double value to check.
   * @return true if it's matched otherwise false.
   */
  bool match(double value) const;

private:
  const envoy::type::matcher::DoubleMatcher matcher_;
};

class StringMatcher {
public:
  StringMatcher(const envoy::type::matcher::StringMatcher& matcher) : matcher_(matcher) {
    if (matcher.match_pattern_case() == envoy::type::matcher::StringMatcher::kRegex) {
      regex_ = RegexUtil::parseRegex(matcher_.regex());
    }
  }

  /**
   * Check whether the value is matched to the matcher.
   * @param value the string to check.
   * @return true if it's matched otherwise false.
   */
  bool match(const std::string& value) const;

private:
  const envoy::type::matcher::StringMatcher matcher_;
  std::regex regex_;
};

class MetadataMatcher {
public:
  MetadataMatcher(const envoy::type::matcher::MetadataMatcher& matcher);

  /**
   * Check whether the metadata is matched to the matcher.
   * @param metadata the metadata to check.
   * @return true if it's matched otherwise false.
   */
  bool match(const envoy::api::v2::core::Metadata& metadata) const;

private:
  const envoy::type::matcher::MetadataMatcher matcher_;
  std::vector<std::string> path_;

  bool null_matcher_{false};
  absl::optional<bool> bool_matcher_;
  bool present_matcher_{false};

  absl::optional<DoubleMatcher> double_matcher_;
  absl::optional<StringMatcher> string_matcher_;
};

} // namespace Matchers
} // namespace Envoy
