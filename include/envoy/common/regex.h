#pragma once

#include <memory>

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Regex {

/**
 * A compiled regex expression matcher which uses an abstract regex engine.
 */
class CompiledMatcher {
public:
  virtual ~CompiledMatcher() = default;

  /**
   * @return whether the value matches the compiled regex expression.
   */
  virtual bool match(absl::string_view value) const PURE;
};

using CompiledMatcherPtr = std::unique_ptr<const CompiledMatcher>;

} // namespace Regex
} // namespace Envoy
