#pragma once

#include <memory>

#include "envoy/common/optref.h"
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

  class Context {
  public:
    virtual ~Context() = default;
  };

  /**
   * Return whether a passed string value matches.
   */
  virtual bool match(const absl::string_view value,
                     OptRef<const Context> context = absl::nullopt) const PURE;
};

using StringMatcherPtr = std::unique_ptr<const StringMatcher>;

} // namespace Matchers
} // namespace Envoy
