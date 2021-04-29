#pragma once

#include <string>

#include "envoy/common/lower_case_string.h"
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
   * @param key The context key of string view type.
   * @return The optional context value of string_view type.
   */
  virtual absl::optional<absl::string_view> getTraceContext(absl::string_view key) const PURE;

  /**
   * Set new tracing context key/value pair.
   *
   * @param key The context key of string view type.
   * @param value The context value of string view type.
   */
  virtual void setTraceContext(absl::string_view key, absl::string_view value) PURE;

  /**
   * Get tracing context value by case-insensitive key.
   *
   * @param key The case-insensitive context key.
   * @return The optional context value of string_view type.
   */
  virtual absl::optional<absl::string_view> getTraceContext(const LowerCaseString& key) const {
    return getTraceContext(key.get());
  }

  /**
   * Set new tracing context key/value pair.
   *
   * @param key The case-insensitive context key.
   * @param value The context value of string view type.
   */
  virtual void setTraceContext(const LowerCaseString& key, absl::string_view value) {
    setTraceContext(key.get(), value);
  }
};

} // namespace Tracing
} // namespace Envoy
