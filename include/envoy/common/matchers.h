#pragma once

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Matchers {

// fixfix comments
class StringMatcher {
public:
  virtual ~StringMatcher() = default;

  virtual bool match(const absl::string_view value) const PURE;
};

using StringMatcherPtr = std::unique_ptr<const StringMatcher>;

} // namespace Matchers
} // namespace Envoy
