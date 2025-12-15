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

#include "source/common/common/filter_state_object_matchers.h"
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
  virtual bool match(const Protobuf::Value& value) const PURE;

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
  bool match(const Protobuf::Value& value) const override;
};

class BoolMatcher : public ValueMatcher {
public:
  BoolMatcher(bool matcher) : matcher_(matcher) {}

  bool match(const Protobuf::Value& value) const override;

private:
  const bool matcher_;
};

class PresentMatcher : public ValueMatcher {
public:
  PresentMatcher(bool matcher) : matcher_(matcher) {}

  bool match(const Protobuf::Value& value) const override;

private:
  const bool matcher_;
};

class DoubleMatcher : public ValueMatcher {
public:
  DoubleMatcher(const envoy::type::matcher::v3::DoubleMatcher& matcher) : matcher_(matcher) {}

  bool match(const Protobuf::Value& value) const override;

private:
  const envoy::type::matcher::v3::DoubleMatcher matcher_;
};

class UniversalStringMatcher : public StringMatcher {
public:
  // To avoid hiding other implementations of match.
  using StringMatcher::match;

  bool match(absl::string_view) const override { return true; }
};

StringMatcherPtr getExtensionStringMatcher(const ::xds::core::v3::TypedExtensionConfig& config,
                                           Server::Configuration::CommonFactoryContext& context);

// A matcher for the `exact` StringMatcher.
class ExactStringMatcher {
public:
  ExactStringMatcher(absl::string_view exact, bool ignore_case)
      : exact_(exact), ignore_case_(ignore_case) {}

  // StringMatcher
  bool match(absl::string_view value, OptRef<const StringMatcher::Context>) const {
    return ignore_case_ ? absl::EqualsIgnoreCase(value, exact_) : value == exact_;
  }

  const std::string& stringRepresentation() const { return exact_; }

private:
  const std::string exact_;
  const bool ignore_case_;
};

// A matcher for the `prefix` StringMatcher.
class PrefixStringMatcher {
public:
  PrefixStringMatcher(absl::string_view prefix, bool ignore_case)
      : prefix_(prefix), ignore_case_(ignore_case) {}

  // StringMatcher
  bool match(absl::string_view value, OptRef<const StringMatcher::Context>) const {
    return ignore_case_ ? absl::StartsWithIgnoreCase(value, prefix_)
                        : absl::StartsWith(value, prefix_);
  }

  const std::string& stringRepresentation() const { return prefix_; }

private:
  friend class StringMatcherImpl;
  const std::string prefix_;
  const bool ignore_case_;
};

// A matcher for the `suffix` StringMatcher.
class SuffixStringMatcher {
public:
  SuffixStringMatcher(absl::string_view suffix, bool ignore_case)
      : suffix_(suffix), ignore_case_(ignore_case) {}

  // StringMatcher
  bool match(absl::string_view value, OptRef<const StringMatcher::Context>) const {
    return ignore_case_ ? absl::EndsWithIgnoreCase(value, suffix_) : absl::EndsWith(value, suffix_);
  }

  const std::string& stringRepresentation() const { return suffix_; }

private:
  const std::string suffix_;
  const bool ignore_case_;
};

// A matcher for the `safe_regex` StringMatcher.
class RegexStringMatcher {
public:
  // The RegexMatcher can either be from the ::envoy or ::xds type,
  // and the templated c'tor handles both cases.
  template <class RegexMatcherType>
  RegexStringMatcher(const RegexMatcherType& safe_regex,
                     Server::Configuration::CommonFactoryContext& context);

  RegexStringMatcher(RegexStringMatcher&& other) noexcept { regex_ = std::move(other.regex_); }

  // StringMatcher
  bool match(absl::string_view value, OptRef<const StringMatcher::Context>) const {
    return regex_->match(value);
  }

  const std::string& stringRepresentation() const { return regex_->pattern(); }

private:
  Regex::CompiledMatcherPtr regex_;
};

// A matcher for the `contains` StringMatcher.
class ContainsStringMatcher {
public:
  ContainsStringMatcher(absl::string_view contents, bool ignore_case)
      : contents_(ignore_case ? absl::AsciiStrToLower(contents) : contents),
        ignore_case_(ignore_case) {}

  // StringMatcher
  bool match(absl::string_view value, OptRef<const StringMatcher::Context>) const {
    return ignore_case_ ? absl::StrContains(absl::AsciiStrToLower(value), contents_)
                        : absl::StrContains(value, contents_);
  }

  const std::string& stringRepresentation() const {
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
class CustomStringMatcher {
public:
  CustomStringMatcher(const xds::core::v3::TypedExtensionConfig& custom,
                      Server::Configuration::CommonFactoryContext& context)
      : custom_(getExtensionStringMatcher(custom, context)) {}

  // StringMatcher
  bool match(absl::string_view value, OptRef<const StringMatcher::Context> context) const {
    if (context) {
      return custom_->match(value, context.ref());
    }

    return custom_->match(value);
  }

  const std::string& stringRepresentation() const { return EMPTY_STRING; }

private:
  StringMatcherPtr custom_;
};

// An implementation that holds one of the string matchers.
class StringMatcherImpl : public ValueMatcher, public StringMatcher {
public:
  // Creates a StringMatcher given a config.
  // Note that Envoy supports both matchers that are created using the
  // `envoy::type::matcher::v3::StringMatcher` type and the
  // `xds::core::v3::StringMatcher` type and therefore this function is templated.
  template <class StringMatcherType = envoy::type::matcher::v3::StringMatcher>
  explicit StringMatcherImpl(const StringMatcherType& matcher,
                             Server::Configuration::CommonFactoryContext& context)
      : matcher_(createVariant(matcher, context)) {}

  // Helper to create an exact matcher in contexts where there is no factory context available.
  // This is a static member instead of constructor so that it can be named for clarity of what it
  // produces.
  static StringMatcherImpl createExactMatcher(absl::string_view match) {
    return StringMatcherImpl(ExactStringMatcher(match, false));
  }

  // StringMatcher
  bool match(absl::string_view value) const override { return doMatch(value, absl::nullopt); }
  bool match(absl::string_view value, const StringMatcher::Context& context) const override {
    return doMatch(value, makeOptRef(context));
  }

  // ValueMatcher
  bool match(const Protobuf::Value& value) const override {
    if (value.kind_case() != Protobuf::Value::kStringValue) {
      return false;
    }

    return match(value.string_value());
  }

  /**
   * Helps applications optimize the case where a matcher is a case-sensitive
   * prefix-match.
   *
   * @param prefix the returned prefix string
   * @return true if the matcher is a case-sensitive prefix-match.
   */
  bool getCaseSensitivePrefixMatch(std::string& prefix) const {
    if (const PrefixStringMatcher* prefix_matcher = absl::get_if<PrefixStringMatcher>(&matcher_)) {
      if (!prefix_matcher->ignore_case_) {
        prefix = prefix_matcher->prefix_;
        return true;
      }
    }
    return false;
  }

  /**
   * Returns a string representation of the matcher (the contents to be
   * matched).
   */
  const std::string& stringRepresentation() const {
    // Implementing polymorphism for stringRepresentation() on the different
    // types that can be in the matcher_ variant.
    auto call_func = [](const auto& obj) -> const std::string& {
      return obj.stringRepresentation();
    };
    return absl::visit(call_func, matcher_);
  }

private:
  explicit StringMatcherImpl(ExactStringMatcher&& exact_matcher)
      : matcher_(std::move(exact_matcher)) {}

  using StringMatcherVariant =
      absl::variant<ExactStringMatcher, PrefixStringMatcher, SuffixStringMatcher,
                    RegexStringMatcher, ContainsStringMatcher, CustomStringMatcher>;

  template <class StringMatcherType = envoy::type::matcher::v3::StringMatcher>
  static StringMatcherVariant createVariant(const StringMatcherType& matcher,
                                            Server::Configuration::CommonFactoryContext& context);
  bool doMatch(absl::string_view value, OptRef<const StringMatcher::Context> context) const {
    // Implementing polymorphism for match(absl::string_value) on the different
    // types that can be in the matcher_ variant.
    auto call_match = [value, context](const auto& obj) -> bool {
      return obj.match(value, context);
    };

    return absl::visit(call_match, matcher_);
  }

  StringMatcherVariant matcher_;
};

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

  bool match(const Protobuf::Value& value) const override;

private:
  ValueMatcherConstSharedPtr oneof_value_matcher_;
};

class OrMatcher : public ValueMatcher {
public:
  OrMatcher(const envoy::type::matcher::v3::OrMatcher& matcher,
            Server::Configuration::CommonFactoryContext& context);

  bool match(const Protobuf::Value& value) const override;

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
  FilterStateMatcher(std::string key, FilterStateObjectMatcherPtr&& object_matcher);

  /**
   * Check whether the filter state object is matched to the matcher.
   * @param filter state to check.
   * @return true if it's matched otherwise false.
   */
  bool match(const StreamInfo::FilterState& filter_state) const;

  static absl::StatusOr<std::unique_ptr<FilterStateMatcher>>
  create(const envoy::type::matcher::v3::FilterStateMatcher& matcher,
         Server::Configuration::CommonFactoryContext& context);

private:
  const std::string key_;
  FilterStateObjectMatcherPtr object_matcher_;
};

using FilterStateMatcherPtr = std::unique_ptr<FilterStateMatcher>;

class PathMatcher : public StringMatcher {
public:
  PathMatcher(const envoy::type::matcher::v3::PathMatcher& path,
              Server::Configuration::CommonFactoryContext& context)
      : matcher_(path.path(), context) {}
  PathMatcher(const envoy::type::matcher::v3::StringMatcher& matcher,
              Server::Configuration::CommonFactoryContext& context)
      : matcher_(matcher, context) {}

  // To avoid hiding other implementations of match.
  using StringMatcher::match;

  static PathMatcherConstSharedPtr
  createExact(const std::string& exact, bool ignore_case,
              Server::Configuration::CommonFactoryContext& context);
  static PathMatcherConstSharedPtr
  createPrefix(const std::string& prefix, bool ignore_case,
               Server::Configuration::CommonFactoryContext& context);
  static PathMatcherConstSharedPtr
  createSafeRegex(const envoy::type::matcher::v3::RegexMatcher& regex_matcher,
                  Server::Configuration::CommonFactoryContext& context);

  bool match(absl::string_view path) const override;
  const std::string& stringRepresentation() const { return matcher_.stringRepresentation(); }

private:
  const StringMatcherImpl matcher_;
};

} // namespace Matchers
} // namespace Envoy
