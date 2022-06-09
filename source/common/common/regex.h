#pragma once

#include <memory>
#include <regex>

#include "envoy/common/regex.h"
#include "envoy/type/matcher/v3/regex.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/regex_engine.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stats/symbol_table.h"

#include "re2/re2.h"
#include "xds/type/matcher/v3/regex.pb.h"

namespace Envoy {
namespace Regex {

class CompiledGoogleReMatcher : public CompiledMatcher {
public:
  explicit CompiledGoogleReMatcher(const std::string& regex, bool do_program_size_check);

  explicit CompiledGoogleReMatcher(const std::string& regex, uint32_t max_program_size,
                                   uint32_t warn_program_size);

  explicit CompiledGoogleReMatcher(const xds::type::matcher::v3::RegexMatcher& config)
      : CompiledGoogleReMatcher(config.regex(), false) {}

  explicit CompiledGoogleReMatcher(const envoy::type::matcher::v3::RegexMatcher& config);

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

class GoogleReEngine : public EngineBase {
public:
  CompiledMatcherPtr matcher(const std::string& regex) const override;

  // Server::Configuration::BootstrapExtensionFactory
  Server::BootstrapExtensionPtr
  createBootstrapExtension(const Protobuf::Message& config,
                           Server::Configuration::ServerFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override { return "envoy.extensions.regex_engine.google_re2"; };

private:
  uint32_t max_program_size_{100};
  uint32_t warn_program_size_{UINT32_MAX};
};

DECLARE_FACTORY(GoogleReEngine);

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
    // Fallback deprecated engine type in regex matcher.
    if (matcher.has_google_re2()) {
      return std::make_unique<CompiledGoogleReMatcher>(matcher);
    }

    return EngineSingleton::get().matcher(matcher.regex());
  }
};

} // namespace Regex
} // namespace Envoy
