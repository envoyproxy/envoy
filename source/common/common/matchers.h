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

StringMatcherPtr getExtensionStringMatcher(const ::xds::core::v3::TypedExtensionConfig& config,
                                           ThreadLocal::SlotAllocator& tls, Api::Api& api);

template <class StringMatcherType = envoy::type::matcher::v3::StringMatcher>
class PrivateStringMatcherImpl : public ValueMatcher, public StringMatcher {
public:
  // TODO(ggreenway): convert all but the first parameter into
  // `Server::Configuration::CommonFactoryContext`.
  explicit PrivateStringMatcherImpl(const StringMatcherType& matcher, Regex::Engine* regex_engine,
                                    ThreadLocal::SlotAllocator* tls, Api::Api* api)
      : matcher_(matcher) {
    if (matcher.match_pattern_case() == StringMatcherType::MatchPatternCase::kSafeRegex) {
      if (matcher.ignore_case()) {
        ExceptionUtil::throwEnvoyException("ignore_case has no effect for safe_regex.");
      }
      if (regex_engine != nullptr) {
        regex_ = Regex::Utility::parseRegex(matcher_.safe_regex(), *regex_engine);
      } else {
        // TODO(ggreenway): remove this branch when we always have an engine. This is only
        // needed to make tests not complain about dereferencing a null pointer, even though
        // the reference isn't actually used.
        regex_ = Regex::Utility::parseRegex(matcher_.safe_regex());
      }
    } else if (matcher.match_pattern_case() == StringMatcherType::MatchPatternCase::kContains) {
      if (matcher_.ignore_case()) {
        // Cache the lowercase conversion of the Contains matcher for future use
        lowercase_contains_match_ = absl::AsciiStrToLower(matcher_.contains());
      }
    } else if (matcher.has_custom()) {
      custom_ = getExtensionStringMatcher(matcher.custom(), *tls, *api);
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
    case StringMatcherType::MatchPatternCase::kCustom:
      return custom_->match(value);
    default:
      PANIC("unexpected");
    }
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
  StringMatcherPtr custom_;
};

// Temporarily create two separate types with different constructors, inheriting from the same
// implementation, to make it easier to find and replace all usage of the old one.
// TODO(ggreenway): delete these two extra classes, make `PrivateStringMatcherImpl` back into
// `StringMatcherImpl`.
template <class StringMatcherType = envoy::type::matcher::v3::StringMatcher>
class StringMatcherImplWithContext : public PrivateStringMatcherImpl<StringMatcherType> {
public:
  explicit StringMatcherImplWithContext(const StringMatcherType& matcher,
                                        Server::Configuration::CommonFactoryContext& context)
      : PrivateStringMatcherImpl<StringMatcherType>(matcher, &context.regexEngine(),
                                                    &context.threadLocal(), &context.api()) {}
};

template <class StringMatcherType = envoy::type::matcher::v3::StringMatcher>
class StringMatcherImpl : public PrivateStringMatcherImpl<StringMatcherType> {
public:
  explicit StringMatcherImpl(const StringMatcherType& matcher)
      : PrivateStringMatcherImpl<StringMatcherType>(
            matcher, Regex::EngineSingleton::getExisting(),
            InjectableSingleton<ThreadLocal::SlotAllocator>::getExisting(),
            InjectableSingleton<Api::Api>::getExisting()) {}
};

class StringMatcherExtensionFactory : public Config::TypedFactory {
public:
  // TODO(ggreenway): Convert all but first parameter to `CommonFactoryContext`.
  virtual StringMatcherPtr createStringMatcher(const ProtobufWkt::Any& config,
                                               ThreadLocal::SlotAllocator& tls, Api::Api& api) PURE;

  std::string category() const override { return "envoy.string_matcher"; }
};

class ListMatcher : public ValueMatcher {
public:
  ListMatcher(const envoy::type::matcher::v3::ListMatcher& matcher);

  bool match(const ProtobufWkt::Value& value) const override;

private:
  const envoy::type::matcher::v3::ListMatcher matcher_;

  ValueMatcherConstSharedPtr oneof_value_matcher_;
};

class OrMatcher : public ValueMatcher {
public:
  OrMatcher(const envoy::type::matcher::v3::OrMatcher& matcher);

  bool match(const ProtobufWkt::Value& value) const override;

private:
  std::vector<ValueMatcherConstSharedPtr> or_matchers_;
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
  const StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>& matcher() const {
    return matcher_;
  }

private:
  const StringMatcherImpl<envoy::type::matcher::v3::StringMatcher> matcher_;
};

} // namespace Matchers
} // namespace Envoy
