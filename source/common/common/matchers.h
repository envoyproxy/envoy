#pragma once

#include <string>

#include "envoy/common/exception.h"
#include "envoy/common/matchers.h"
#include "envoy/common/regex.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/type/matcher/v3/filter_state.pb.h"
#include "envoy/type/matcher/v3/metadata.pb.h"
#include "envoy/type/matcher/v3/number.pb.h"
#include "envoy/type/matcher/v3/path.pb.h"
#include "envoy/type/matcher/v3/string.pb.h"
#include "envoy/type/matcher/v3/value.pb.h"

#include "source/common/common/regex.h"
#include "source/common/common/utility.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Matchers {

class ValueMatcher;
using ValueMatcherConstSharedPtr = std::shared_ptr<const ValueMatcher>;

class PathMatcher;
using PathMatcherConstSharedPtr = std::shared_ptr<const PathMatcher>;

class ValueMatcher {
public:
  virtual ~ValueMatcher() = default;

  /**
   * Check whether the value is matched to the matcher.
   */
  virtual bool match(const ProtobufWkt::Value& value) const PURE;

  /**
   * Create the matcher object.
   */
  static ValueMatcherConstSharedPtr create(const envoy::type::matcher::v3::ValueMatcher& value);
};

class NullMatcher : public ValueMatcher {
public:
  /**
   * Check whether the value is NULL.
   */
  bool match(const ProtobufWkt::Value& value) const override;
};

class BoolMatcher : public ValueMatcher {
public:
  BoolMatcher(bool matcher) : matcher_(matcher) {}

  bool match(const ProtobufWkt::Value& value) const override;

private:
  const bool matcher_;
};

class PresentMatcher : public ValueMatcher {
public:
  PresentMatcher(bool matcher) : matcher_(matcher) {}

  bool match(const ProtobufWkt::Value& value) const override;

private:
  const bool matcher_;
};

class DoubleMatcher : public ValueMatcher {
public:
  DoubleMatcher(const envoy::type::matcher::v3::DoubleMatcher& matcher) : matcher_(matcher) {}

  bool match(const ProtobufWkt::Value& value) const override;

private:
  const envoy::type::matcher::v3::DoubleMatcher matcher_;
};

class UniversalStringMatcher : public StringMatcher {
public:
  bool match(absl::string_view) const override { return true; }
};

template <class StringMatcherType = envoy::type::matcher::v3::StringMatcher>
class StringMatcherImpl : public ValueMatcher, public StringMatcher {
public:
  explicit StringMatcherImpl(const StringMatcherType& matcher) : matcher_(matcher) {
    if (matcher.match_pattern_case() == StringMatcherType::MatchPatternCase::kSafeRegex) {
      if (matcher.ignore_case()) {
        ExceptionUtil::throwEnvoyException("ignore_case has no effect for safe_regex.");
      }
      regex_ = Regex::Utility::parseRegex(matcher_.safe_regex());
    } else if (matcher.match_pattern_case() == StringMatcherType::MatchPatternCase::kContains) {
      if (matcher_.ignore_case()) {
        // Cache the lowercase conversion of the Contains matcher for future use
        lowercase_contains_match_ = absl::AsciiStrToLower(matcher_.contains());
      }
    }
  }

  // StringMatcher
  bool match(const absl::string_view value) const override {
    switch (matcher_.match_pattern_case()) {
    case StringMatcherType::MatchPatternCase::kExact:
      return matcher_.ignore_case() ? absl::EqualsIgnoreCase(value, matcher_.exact())
                                    : value == matcher_.exact();
    case StringMatcherType::MatchPatternCase::kPrefix:
      return matcher_.ignore_case() ? absl::StartsWithIgnoreCase(value, matcher_.prefix())
                                    : absl::StartsWith(value, matcher_.prefix());
    case StringMatcherType::MatchPatternCase::kSuffix:
      return matcher_.ignore_case() ? absl::EndsWithIgnoreCase(value, matcher_.suffix())
                                    : absl::EndsWith(value, matcher_.suffix());
    case StringMatcherType::MatchPatternCase::kContains:
      return matcher_.ignore_case()
                 ? absl::StrContains(absl::AsciiStrToLower(value), lowercase_contains_match_)
                 : absl::StrContains(value, matcher_.contains());
    case StringMatcherType::MatchPatternCase::kSafeRegex:
      return regex_->match(value);
    case StringMatcherType::MatchPatternCase::MATCH_PATTERN_NOT_SET:
      break;
    }
    PANIC("unexpected");
  }
  bool match(const ProtobufWkt::Value& value) const override {

    if (value.kind_case() != ProtobufWkt::Value::kStringValue) {
      return false;
    }

    return match(value.string_value());
  }

  const StringMatcherType& matcher() const { return matcher_; }

  /**
   * Helps applications optimize the case where a matcher is a case-sensitive
   * prefix-match.
   *
   * @param prefix the returned prefix string
   * @return true if the matcher is a case-sensitive prefix-match.
   */
  bool getCaseSensitivePrefixMatch(std::string& prefix) const {
    if (matcher_.match_pattern_case() ==
            envoy::type::matcher::v3::StringMatcher::MatchPatternCase::kPrefix &&
        !matcher_.ignore_case()) {
      prefix = matcher_.prefix();
      return true;
    }
    return false;
  }

private:
  const StringMatcherType matcher_;
  Regex::CompiledMatcherPtr regex_;
  std::string lowercase_contains_match_;
};

class ListMatcher : public ValueMatcher {
public:
  ListMatcher(const envoy::type::matcher::v3::ListMatcher& matcher);

  bool match(const ProtobufWkt::Value& value) const override;

private:
  const envoy::type::matcher::v3::ListMatcher matcher_;

  ValueMatcherConstSharedPtr oneof_value_matcher_;
};

class MetadataMatcher {
public:
  MetadataMatcher(const envoy::type::matcher::v3::MetadataMatcher& matcher);

  /**
   * Check whether the metadata is matched to the matcher.
   * @param metadata the metadata to check.
   * @return true if it's matched otherwise false.
   */
  bool match(const envoy::config::core::v3::Metadata& metadata) const;

private:
  const envoy::type::matcher::v3::MetadataMatcher matcher_;
  std::vector<std::string> path_;

  ValueMatcherConstSharedPtr value_matcher_;
};

class FilterStateMatcher {
public:
  FilterStateMatcher(const envoy::type::matcher::v3::FilterStateMatcher& matcher);

  /**
   * Check whether the filter state object is matched to the matcher.
   * @param filter state to check.
   * @return true if it's matched otherwise false.
   */
  bool match(const StreamInfo::FilterState& filter_state) const;

private:
  const std::string key_;
  const StringMatcherPtr value_matcher_;
};

class PathMatcher : public StringMatcher {
public:
  PathMatcher(const envoy::type::matcher::v3::PathMatcher& path) : matcher_(path.path()) {}
  PathMatcher(const envoy::type::matcher::v3::StringMatcher& matcher) : matcher_(matcher) {}

  static PathMatcherConstSharedPtr createExact(const std::string& exact, bool ignore_case);
  static PathMatcherConstSharedPtr createPrefix(const std::string& prefix, bool ignore_case);
  static PathMatcherConstSharedPtr createPattern(const std::string& pattern, bool ignore_case);
  static PathMatcherConstSharedPtr
  createSafeRegex(const envoy::type::matcher::v3::RegexMatcher& regex_matcher);

  bool match(const absl::string_view path) const override;

private:
  const StringMatcherImpl<envoy::type::matcher::v3::StringMatcher> matcher_;
};

} // namespace Matchers
} // namespace Envoy
