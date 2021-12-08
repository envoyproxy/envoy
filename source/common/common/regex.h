#pragma once

#include <memory>
#include <regex>

#include "envoy/common/regex.h"
#include "envoy/runtime/runtime.h"
#include "envoy/type/matcher/v3/regex.pb.h"

#include "source/common/common/assert.h"
#include "source/common/config/registry_utils.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stats/symbol_table_impl.h"

#include "re2/re2.h"
#include "xds/type/matcher/v3/regex.pb.h"
#include "xds/type/matcher/v3/regex.pb.validate.h"

namespace Envoy {
namespace Regex {

class CompiledGoogleReMatcher : public CompiledMatcher {
public:
  explicit CompiledGoogleReMatcher(const std::string& regex, bool do_program_size_check);

  explicit CompiledGoogleReMatcher(const std::string& regex,
                                   const xds::type::matcher::v3::RegexMatcher::GoogleRE2&)
      : CompiledGoogleReMatcher(regex, false) {}
  explicit CompiledGoogleReMatcher(const std::string& regex,
                                   const envoy::type::matcher::v3::RegexMatcher::GoogleRE2& config);

  // CompiledMatcher
  bool match(absl::string_view value) const override {
    return re2::RE2::FullMatch(re2::StringPiece(value.data(), value.size()), regex_);
  }

  // CompiledMatcher
  std::string replaceAll(absl::string_view value, absl::string_view substitution) const override {
    std::string result = std::string(value);
    re2::RE2::GlobalReplace(&result, regex_,
                            re2::StringPiece(substitution.data(), substitution.size()));
    return result;
  }

private:
  const re2::RE2 regex_;
};

class CompiledGoogleReMatcherFactory : public CompiledMatcherFactory {
public:
  std::string name() const override { return "google-re2"; }

  CompiledMatcherFactoryCb
  createCompiledMatcherFactoryCb(const Protobuf::Message& config,
                                 ProtobufMessage::ValidationVisitor& validation_visitor) override {
    const auto& typed_config =
        MessageUtil::downcastAndValidate<const xds::type::matcher::v3::RegexMatcher::GoogleRE2&>(
            config, validation_visitor);

    return [typed_config](const std::string& regex) {
      return std::make_unique<CompiledGoogleReMatcher>(regex, typed_config);
    };
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<xds::type::matcher::v3::RegexMatcher::GoogleRE2>();
  }
};

enum class Type { Re2, StdRegex };

/**
 * Utilities for constructing regular expressions.
 */
class Utility {
public:
  /**
   * Constructs a std::regex, converting any std::regex_error exception into an EnvoyException.
   * @param regex std::string containing the regular expression to parse.
   * @param flags std::regex::flag_type containing parser flags. Defaults to std::regex::optimize.
   * @return std::regex constructed from regex and flags.
   * @throw EnvoyException if the regex string is invalid.
   */
  static std::regex parseStdRegex(const std::string& regex,
                                  std::regex::flag_type flags = std::regex::optimize);

  /**
   * Construct a compiled regex matcher from a match config.
   */
  template <class RegexMatcherType>
  static CompiledMatcherPtr parseRegex(const RegexMatcherType& matcher) {
    // Google Re is the only currently supported engine.
    ASSERT(matcher.has_google_re2());
    return std::make_unique<CompiledGoogleReMatcher>(matcher.regex(), matcher.google_re2());
  }

  /**
   * Construct a compiled regex matcher from a match config.
   */
  template <class RegexMatcherType = xds::type::matcher::v3::RegexMatcher>
  static CompiledMatcherPtr parseRegex(const RegexMatcherType& matcher,
                                       ProtobufMessage::ValidationVisitor& validation_visitor) {
    // Fall back to deprecated google_re2 field.
    if (matcher.has_google_re2()) {
      return parseRegex(matcher);
    }

    auto* factory = Config::RegistryUtils::getFactory<CompiledMatcherFactory>(matcher.engine());
    ASSERT(factory);

    ProtobufTypes::MessagePtr message = Config::RegistryUtils::translateAnyToFactoryConfig(
        matcher.engine().typed_config(), validation_visitor, *factory);
    auto regex_matcher = factory->createCompiledMatcherFactoryCb(*message, validation_visitor);

    return regex_matcher(matcher.regex());
  }
};

} // namespace Regex
} // namespace Envoy
