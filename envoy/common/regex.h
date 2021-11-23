#pragma once

#include <memory>

#include "envoy/common/matchers.h"
#include "envoy/config/typed_config.h"
#include "envoy/protobuf/message_validator.h"

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
using CompiledMatcherFactoryCb = std::function<CompiledMatcherPtr(const std::string&)>;

class CompiledMatcherFactory : public Config::TypedFactory {
public:
  virtual CompiledMatcherFactoryCb
  createCompiledMatcherFactoryCb(const Protobuf::Message& config,
                                 ProtobufMessage::ValidationVisitor& validation_visitor) PURE;

  std::string category() const override { return "envoy.regex.compiled_matchers"; }
};

} // namespace Regex
} // namespace Envoy
