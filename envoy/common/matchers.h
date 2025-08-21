#pragma once

#include <memory>

#include "envoy/common/optref.h"
#include "envoy/common/pure.h"
#include "envoy/stream_info/stream_info.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Matchers {

/**
 * Generic string matching interface.
 */
class StringMatcher {
public:
  virtual ~StringMatcher() = default;

  struct Context {
    OptRef<const StreamInfo::StreamInfo> stream_info_;
  };

  /**
   * Return whether a passed string value matches.
   */
  virtual bool match(absl::string_view value) const PURE;

  /**
   * Return whether a passed string value matches with context.
   * Because most implementations don't use the context, provides a default implementation.
   */
  virtual bool match(absl::string_view value, const Context&) const { return match(value); }
};

using StringMatcherPtr = std::unique_ptr<const StringMatcher>;

} // namespace Matchers
} // namespace Envoy
