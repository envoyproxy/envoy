#pragma once

#include <string>

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Tracing {

/**
 * Protocol-independent abstraction for traceable stream. It hides the differences between different
 * protocol and provides tracer driver with common methods for obtaining and setting the tracing
 * context.
 *
 * TODO(wbpcode): A new interface should be added to obtain general traceable stream information,
 * such as host, RPC method, protocol identification, etc. At the same time, a new interface needs
 * to be added to support traversal of all trace contexts.
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
   * @param val The context value of string view type.
   */
  virtual void setTraceContext(absl::string_view key, absl::string_view val) PURE;

  /**
   * Set new tracing context key/value pair. The key MUST point to data that will live beyond
   * the lifetime of any traceable stream that using the string.
   *
   * @param key The context key of string view type.
   * @param val The context value of string view type.
   */
  virtual void setTraceContextReferenceKey(absl::string_view key, absl::string_view val) {
    // The reference semantics of key and value are ignored by default. Derived classes that wish to
    // use reference semantics to improve performance or reduce memory overhead can override this
    // method.
    setTraceContext(key, val);
  }

  /**
   * Set new tracing context key/value pair. Both key and val MUST point to data that will live
   * beyond the lifetime of any traceable stream that using the string.
   *
   * @param key The context key of string view type.
   * @param val The context value of string view type.
   */
  virtual void setTraceContextReference(absl::string_view key, absl::string_view val) {
    // The reference semantics of key and value are ignored by default. Derived classes that wish to
    // use reference semantics to improve performance or reduce memory overhead can override this
    // method.
    setTraceContext(key, val);
  }
};

} // namespace Tracing
} // namespace Envoy
