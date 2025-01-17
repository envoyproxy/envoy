#pragma once

#include <memory>

#include "envoy/common/matchers.h"
#include "envoy/config/typed_config.h"
#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Regex {

/**
 * A compiled regex expression matcher which uses an abstract regex engine.
 */
class CompiledMatcher : public Matchers::StringMatcher {
public:
  /**
   * Replaces all non-overlapping occurrences of the pattern in "value" with
   * "substitution". The "substitution" string can make references to
   * capture groups in the pattern, using the syntax specific to that
   * regular expression engine.
   */
  virtual std::string replaceAll(absl::string_view value,
                                 absl::string_view substitution) const PURE;
};

using CompiledMatcherPtr = std::unique_ptr<const CompiledMatcher>;

/**
 * A regular expression engine which turns regular expressions into compiled matchers.
 */
class Engine {
public:
  virtual ~Engine() = default;

  /**
   * Create a @ref CompiledMatcher with the given regex expression.
   * @param regex the regex expression match string
   */
  virtual absl::StatusOr<CompiledMatcherPtr> matcher(const std::string& regex) const PURE;
};

using EnginePtr = std::shared_ptr<Engine>;

/**
 * Factory for Engine.
 */
class EngineFactory : public Config::TypedFactory {
public:
  /**
   * Creates an Engine from the provided config.
   */
  virtual EnginePtr
  createEngine(const Protobuf::Message& config,
               Server::Configuration::ServerFactoryContext& server_factory_context) PURE;

  std::string category() const override { return "envoy.regex_engines"; }
};

} // namespace Regex
} // namespace Envoy
