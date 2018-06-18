#pragma once

#include <string>

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/type/matchers/metadata.pb.h"
#include "envoy/type/matchers/number.pb.h"
#include "envoy/type/matchers/string.pb.h"

#include "common/common/utility.h"

namespace Envoy {
namespace Matchers {

class DoubleMatcher {
public:
  DoubleMatcher(const envoy::type::matchers::DoubleMatcher& matcher) : matcher_(matcher) {}

  /**
   * Check whether the value is matched to the matcher.
   * @param value the double value to check.
   * @return true if it's matched otherwise false.
   */
  bool match(double value) const;

private:
  const envoy::type::matchers::DoubleMatcher matcher_;
};

class StringMatcher {
public:
  StringMatcher(const envoy::type::matchers::StringMatcher& matcher) : matcher_(matcher) {
    if (matcher.match_pattern_case() == envoy::type::matchers::StringMatcher::kRegex) {
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
  const envoy::type::matchers::StringMatcher matcher_;
  std::regex regex_;
};

class MetadataMatcher {
public:
  MetadataMatcher(const envoy::type::matchers::MetadataMatcher& matcher);

  /**
   * Check whether the metadata is matched to the matcher.
   * @param metadata the metadata to check.
   * @return true if it's matched otherwise false.
   */
  bool match(const envoy::api::v2::core::Metadata& metadata) const;

private:
  const envoy::type::matchers::MetadataMatcher matcher_;
  const std::vector<std::string> path_;

  bool null_matcher_{false};
  bool bool_matcher_allow_true_{false};
  bool bool_matcher_allow_false_{false};
  bool present_matcher_{false};

  std::vector<DoubleMatcher> double_matcher_;
  std::vector<StringMatcher> string_matcher_;
};

} // namespace Matchers
} // namespace Envoy
