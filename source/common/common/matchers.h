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
  static ValueMatcherConstSharedPtr create(const envoy::type::matcher::v3::ValueMatcher& value,
                                           Server::Configuration::CommonFactoryContext& context);
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

// A StringMatcher that matches all given strings (similar to the regex ".*").
class UniversalStringMatcher : public StringMatcher {
public:
  bool match(absl::string_view) const override { return true; }

  const std::string& stringRepresentation() const override { return EMPTY_STRING; }
};

StringMatcherPtr getExtensionStringMatcher(const ::xds::core::v3::TypedExtensionConfig& config,
                                           Server::Configuration::CommonFactoryContext& context);

// This class will be replaced by StringMatcherImplBase (see below) and will
// follow cleaner polymorphism and encapsulation principles. The class will be
// removed once all its usages are removed.
// TODO(adisuissa): remove this class once there all uses are replaced by
// StringMatcherImplBase/StringMatcherPtr.
template <class StringMatcherType = envoy::type::matcher::v3::StringMatcher>
class StringMatcherImpl : public ValueMatcher, public StringMatcher {
public:
  explicit StringMatcherImpl(const StringMatcherType& matcher,
                             Server::Configuration::CommonFactoryContext& context)
      : matcher_(matcher) {
    if (matcher.match_pattern_case() == StringMatcherType::MatchPatternCase::kSafeRegex) {
      if (matcher.ignore_case()) {
        ExceptionUtil::throwEnvoyException("ignore_case has no effect for safe_regex.");
      }
      regex_ = THROW_OR_RETURN_VALUE(
          Regex::Utility::parseRegex(matcher_.safe_regex(), context.regexEngine()),
          Regex::CompiledMatcherPtr);
    } else if (matcher.match_pattern_case() == StringMatcherType::MatchPatternCase::kContains) {
      if (matcher_.ignore_case()) {
        // Cache the lowercase conversion of the Contains matcher for future use
        lowercase_contains_match_ = absl::AsciiStrToLower(matcher_.contains());
      }
    } else if (matcher.has_custom()) {
      custom_ = getExtensionStringMatcher(matcher.custom(), context);
    }
  }

  // Helper to create an exact matcher in contexts where there is no factory context available.
  // This is a static member instead of constructor so that it can be named for clarity of what it
  // produces.
  static StringMatcherImpl createExactMatcher(absl::string_view match) {
    return StringMatcherImpl(match);
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

  // TODO(adiuissa): this will be removed once this entire class is replaced by
  // StringMatcherImplBase.
  const std::string& stringRepresentation() const override { return EMPTY_STRING; }

private:
  StringMatcherImpl(absl::string_view exact_match)
      : matcher_([&]() -> StringMatcherType {
          StringMatcherType cfg;
          cfg.set_exact(exact_match);
          return cfg;
        }()) {}

  const StringMatcherType matcher_;
  Regex::CompiledMatcherPtr regex_;
  std::string lowercase_contains_match_;
  StringMatcherPtr custom_;
};

// An abstract common implementation for all types of StringMatchers.
class StringMatcherImplBase : public ValueMatcher, public StringMatcher {
public:
  StringMatcherImplBase() = default;
  virtual ~StringMatcherImplBase() = default;

  // ValueMatcher
  bool match(const ProtobufWkt::Value& value) const override {
    if (value.kind_case() != ProtobufWkt::Value::kStringValue) {
      return false;
    }

    return match(value.string_value());
  }

  // StringMatcher
  virtual bool match(const absl::string_view value) const override PURE;

  /**
   * Helps applications optimize the case where a matcher is a case-sensitive
   * prefix-match.
   *
   * @param prefix the returned prefix string
   * @return true if the matcher is a case-sensitive prefix-match.
   */
  virtual bool getCaseSensitivePrefixMatch(std::string& prefix) const {
    UNREFERENCED_PARAMETER(prefix);
    return false;
  }
};

// A matcher for the `exact` StringMatcher.
class ExactStringMatcher : public StringMatcherImplBase {
public:
  ExactStringMatcher(absl::string_view exact, bool ignore_case)
      : exact_(exact), ignore_case_(ignore_case) {}

  // StringMatcher
  bool match(const absl::string_view value) const override {
    return ignore_case_ ? absl::EqualsIgnoreCase(value, exact_) : value == exact_;
  }

  const std::string& stringRepresentation() const override { return exact_; }

private:
  const std::string exact_;
  const bool ignore_case_;
};

// A matcher for the `prefix` StringMatcher.
class PrefixStringMatcher : public StringMatcherImplBase {
public:
  PrefixStringMatcher(absl::string_view prefix, bool ignore_case)
      : prefix_(prefix), ignore_case_(ignore_case) {}

  // StringMatcher
  bool match(const absl::string_view value) const override {
    return ignore_case_ ? absl::StartsWithIgnoreCase(value, prefix_)
                        : absl::StartsWith(value, prefix_);
  }

  // StringMatcherImplBase
  bool getCaseSensitivePrefixMatch(std::string& prefix) const override {
    if (!ignore_case_) {
      prefix = prefix_;
      return true;
    }
    return false;
  }

  const std::string& stringRepresentation() const override { return prefix_; }

private:
  const std::string prefix_;
  const bool ignore_case_;
};

// A matcher for the `suffix` StringMatcher.
class SuffixStringMatcher : public StringMatcherImplBase {
public:
  SuffixStringMatcher(absl::string_view suffix, bool ignore_case)
      : suffix_(suffix), ignore_case_(ignore_case) {}

  // StringMatcher
  bool match(const absl::string_view value) const override {
    return ignore_case_ ? absl::EndsWithIgnoreCase(value, suffix_) : absl::EndsWith(value, suffix_);
  }

  const std::string& stringRepresentation() const override { return suffix_; }

private:
  const std::string suffix_;
  const bool ignore_case_;
};

// A matcher for the `safe_regex` StringMatcher.
class RegexStringMatcher : public StringMatcherImplBase {
public:
  // The RegexMatcher can either be from the ::envoy or ::xds type,
  // and the templated c'tor handles both cases.
  template <class RegexMatcherType>
  RegexStringMatcher(const RegexMatcherType& safe_regex,
                     Server::Configuration::CommonFactoryContext& context)
      : regex_(THROW_OR_RETURN_VALUE(Regex::Utility::parseRegex(safe_regex, context.regexEngine()),
                                     Regex::CompiledMatcherPtr)) {}

  // StringMatcher
  bool match(const absl::string_view value) const override { return regex_->match(value); }

  const std::string& stringRepresentation() const override {
    return regex_->stringRepresentation();
  }

private:
  Regex::CompiledMatcherPtr regex_;
};

// A matcher for the `contains` StringMatcher.
class ContainsStringMatcher : public StringMatcherImplBase {
public:
  ContainsStringMatcher(absl::string_view contents, bool ignore_case)
      : contents_(ignore_case ? absl::AsciiStrToLower(contents) : contents),
        ignore_case_(ignore_case) {}

  // StringMatcher
  bool match(const absl::string_view value) const override {
    return ignore_case_ ? absl::StrContains(absl::AsciiStrToLower(value), contents_)
                        : absl::StrContains(value, contents_);
  }

  const std::string& stringRepresentation() const override {
    // Note that in case where the matcher is configured to be case-insensitive
    // this will return the lower-case configured string (as opposed to the
    // other matchers). This is incompatible with the other matchers, but achieves
    // the intent of this method.
    return contents_;
  }

private:
  // If ignore_case is set the contents_ will contain lower-case letters.
  const std::string contents_;
  const bool ignore_case_;
};

// A matcher for the `custom` StringMatcher.
class CustomStringMatcher : public StringMatcherImplBase {
public:
  CustomStringMatcher(const xds::core::v3::TypedExtensionConfig& custom,
                      Server::Configuration::CommonFactoryContext& context)
      : custom_(getExtensionStringMatcher(custom, context)) {}

  // StringMatcher
  bool match(const absl::string_view value) const override { return custom_->match(value); }

  const std::string& stringRepresentation() const override {
    return custom_->stringRepresentation();
  }

private:
  const StringMatcherPtr custom_;
};

// Creates a StringMatcher given a config.
// Note that Envoy supports both matchers that are created using the
// `envoy::type::matcher::v3::StringMatcher` type and the
// `xds::type::matcher::v3::StringMatcher` type and therefore this function is templated.
template <class StringMatcherType = envoy::type::matcher::v3::StringMatcher>
std::unique_ptr<StringMatcherImplBase>
createStringMatcher(const StringMatcherType& config,
                    Server::Configuration::CommonFactoryContext& context);

class StringMatcherExtensionFactory : public Config::TypedFactory {
public:
  virtual StringMatcherPtr
  createStringMatcher(const Protobuf::Message& config,
                      Server::Configuration::CommonFactoryContext& context) PURE;

  std::string category() const override { return "envoy.string_matcher"; }
};

class ListMatcher : public ValueMatcher {
public:
  ListMatcher(const envoy::type::matcher::v3::ListMatcher& matcher,
              Server::Configuration::CommonFactoryContext& context);

  bool match(const ProtobufWkt::Value& value) const override;

private:
  ValueMatcherConstSharedPtr oneof_value_matcher_;
};

class OrMatcher : public ValueMatcher {
public:
  OrMatcher(const envoy::type::matcher::v3::OrMatcher& matcher,
            Server::Configuration::CommonFactoryContext& context);

  bool match(const ProtobufWkt::Value& value) const override;

private:
  std::vector<ValueMatcherConstSharedPtr> or_matchers_;
};

class MetadataMatcher {
public:
  MetadataMatcher(const envoy::type::matcher::v3::MetadataMatcher& matcher,
                  Server::Configuration::CommonFactoryContext& context);

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
  FilterStateMatcher(const envoy::type::matcher::v3::FilterStateMatcher& matcher,
                     Server::Configuration::CommonFactoryContext& context);

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
  PathMatcher(const envoy::type::matcher::v3::PathMatcher& path,
              Server::Configuration::CommonFactoryContext& context)
      : matcher_(path.path(), context) {}
  PathMatcher(const envoy::type::matcher::v3::StringMatcher& matcher,
              Server::Configuration::CommonFactoryContext& context)
      : matcher_(matcher, context) {}

  static PathMatcherConstSharedPtr
  createExact(const std::string& exact, bool ignore_case,
              Server::Configuration::CommonFactoryContext& context);
  static PathMatcherConstSharedPtr
  createPrefix(const std::string& prefix, bool ignore_case,
               Server::Configuration::CommonFactoryContext& context);
  static PathMatcherConstSharedPtr
  createPattern(const std::string& pattern, bool ignore_case,
                Server::Configuration::CommonFactoryContext& context);
  static PathMatcherConstSharedPtr
  createSafeRegex(const envoy::type::matcher::v3::RegexMatcher& regex_matcher,
                  Server::Configuration::CommonFactoryContext& context);

  bool match(const absl::string_view path) const override;
  const StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>& matcher() const {
    return matcher_;
  }
  // TODO(adisuissa): This method will replace the `matcher()` call above.
  const std::string& stringRepresentation() const override {
    // TODO(adisuissa): replace with:  return matcher_->stringRepresentation();
    // after the type of `matcher_` is replaced.
    return EMPTY_STRING;
  }

private:
  const StringMatcherImpl<envoy::type::matcher::v3::StringMatcher> matcher_;
};

} // namespace Matchers
} // namespace Envoy
