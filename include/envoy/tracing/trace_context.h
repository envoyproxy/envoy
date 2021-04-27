#pragma once

#include <string>

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Tracing {

/**
 * Protocol-independent abstraction for traceable stream. It hides the differences between different
 * protocol requests and provides Tracer Driver with common methods for obtaining and setting the
 * Tracing context.
 */
class TraceContext {
public:
  virtual ~TraceContext() = default;

  /**
   * Get tracing context value by key.
   *
   * @param key The context key of string view type. The context key should be a string view of a
   * const lowercase string.
   * @return The optional context value of string_view type.
   */
  virtual absl::optional<absl::string_view> getTraceContext(absl::string_view key) const PURE;

  /**
   * Set new tracing context key/value pair.
   *
   * @param key The context key of string view type. The context key should be a string view of a
   * const lowercase string with a longer lifetime than the current TraceContext object.
   * @return The optional context value of string_view type.
   */
  virtual void setTraceContext(absl::string_view key, absl::string_view value) PURE;
};

} // namespace Tracing
} // namespace Envoy
