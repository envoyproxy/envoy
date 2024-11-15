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
  // Create, applying re2.max_program_size.error_level and re2.max_program_size.warn_level.
  static absl::StatusOr<std::unique_ptr<CompiledGoogleReMatcher>>
  createAndSizeCheck(const std::string& regex);
  static absl::StatusOr<std::unique_ptr<CompiledGoogleReMatcher>>
  create(const envoy::type::matcher::v3::RegexMatcher& config);
  static absl::StatusOr<std::unique_ptr<CompiledGoogleReMatcher>>
  create(const xds::type::matcher::v3::RegexMatcher& config);

  // CompiledMatcher
  bool match(absl::string_view value) const override { return re2::RE2::FullMatch(value, regex_); }

  // CompiledMatcher
  std::string replaceAll(absl::string_view value, absl::string_view substitution) const override {
    std::string result = std::string(value);
    re2::RE2::GlobalReplace(&result, regex_, substitution);
    return result;
  }

protected:
  explicit CompiledGoogleReMatcher(const std::string& regex) : regex_(regex, re2::RE2::Quiet) {
    ENVOY_BUG(regex_.ok(), "Invalid regex");
  }
  explicit CompiledGoogleReMatcher(const std::string& regex, absl::Status& creation_status,
                                   bool do_program_size_check);
  explicit CompiledGoogleReMatcher(const envoy::type::matcher::v3::RegexMatcher& config,
                                   absl::Status& creation_status);
  explicit CompiledGoogleReMatcher(const xds::type::matcher::v3::RegexMatcher& config,
                                   absl::Status& creation_status)
      : CompiledGoogleReMatcher(config.regex(), creation_status, false) {}

  const re2::RE2 regex_;
};

// Allow creating CompiledGoogleReMatcher without checking for status failures
// for call sites which really really want to.
class CompiledGoogleReMatcherNoSafetyChecks : public CompiledGoogleReMatcher {
public:
  CompiledGoogleReMatcherNoSafetyChecks(const std::string& regex)
      : CompiledGoogleReMatcher(regex) {}
};

class GoogleReEngine : public Engine {
public:
  absl::StatusOr<CompiledMatcherPtr> matcher(const std::string& regex) const override;
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

enum class Type { Re2, StdRegex };

/**
 * Utilities for constructing regular expressions.
 */
class Utility {
public:
  template <class RegexMatcherType>
  static CompiledMatcherPtr parseRegex(const RegexMatcherType& matcher, Engine& engine) {
    // Fallback deprecated engine type in regex matcher.
    if (matcher.has_google_re2()) {
      return THROW_OR_RETURN_VALUE(CompiledGoogleReMatcher::create(matcher),
                                   std::unique_ptr<CompiledGoogleReMatcher>);
    }

    return THROW_OR_RETURN_VALUE(engine.matcher(matcher.regex()), CompiledMatcherPtr);
  }
};

} // namespace Regex
} // namespace Envoy
