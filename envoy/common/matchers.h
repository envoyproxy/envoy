#pragma once

#include <memory>

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Matchers {

/**
 * Generic string matching interface.
 */
class StringMatcher {
public:
  virtual ~StringMatcher() = default;

  /**
   * Return whether a passed string value matches.
   */
  virtual bool match(const absl::string_view value) const PURE;
};

using StringMatcherPtr = std::unique_ptr<const StringMatcher>;

} // namespace Matchers
} // namespace Envoy
