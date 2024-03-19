#pragma once

#include <memory>
#include <regex>

#include "envoy/common/regex.h"
#include "envoy/registry/registry.h"
#include "envoy/type/matcher/v3/regex.pb.h"

#include "source/common/common/assert.h"
#include "source/common/protobuf/utility.h"
#include "source/common/singleton/threadsafe_singleton.h"
#include "source/common/stats/symbol_table.h"

#include "re2/re2.h"
#include "xds/type/matcher/v3/regex.pb.h"

namespace Envoy {
namespace Regex {

class CompiledGoogleReMatcher : public CompiledMatcher {
public:
  explicit CompiledGoogleReMatcher(const std::string& regex, bool do_program_size_check);

  explicit CompiledGoogleReMatcher(const xds::type::matcher::v3::RegexMatcher& config)
      : CompiledGoogleReMatcher(config.regex(), false) {}

  explicit CompiledGoogleReMatcher(const envoy::type::matcher::v3::RegexMatcher& config);

  // CompiledMatcher
  bool match(absl::string_view value) const override { return re2::RE2::FullMatch(value, regex_); }

  // CompiledMatcher
  std::string replaceAll(absl::string_view value, absl::string_view substitution) const override {
    std::string result = std::string(value);
    re2::RE2::GlobalReplace(&result, regex_, substitution);
    return result;
  }

private:
  const re2::RE2 regex_;
};

class GoogleReEngine : public Engine {
public:
  CompiledMatcherPtr matcher(const std::string& regex) const override;
};

class GoogleReEngineFactory : public EngineFactory {
public:
  EnginePtr
  createEngine(const Protobuf::Message& config,
               Server::Configuration::ServerFactoryContext& server_factory_context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
  std::string name() const override { return "envoy.regex_engines.google_re2"; };
};

DECLARE_FACTORY(GoogleReEngineFactory);

using EngineSingleton = InjectableSingleton<Engine>;

enum class Type { Re2, StdRegex };

/**
 * Utilities for constructing regular expressions.
 */
class Utility {
public:
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

  template <class RegexMatcherType>
  static CompiledMatcherPtr parseRegex(const RegexMatcherType& matcher, Engine& engine) {
    // Fallback deprecated engine type in regex matcher.
    if (matcher.has_google_re2()) {
      return std::make_unique<CompiledGoogleReMatcher>(matcher);
    }

    return engine.matcher(matcher.regex());
  }
};

} // namespace Regex
} // namespace Envoy
