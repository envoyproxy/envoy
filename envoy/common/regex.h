#pragma once

#include <memory>

#include "envoy/common/matchers.h"

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
  virtual CompiledMatcherPtr matcher(const std::string& regex) const PURE;
};

} // namespace Regex
} // namespace Envoy
