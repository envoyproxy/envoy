#pragma once

#include <functional>
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
 */
class TraceContext {
public:
  virtual ~TraceContext() = default;

  using IterateCallback = std::function<bool(absl::string_view key, absl::string_view val)>;

  /**
   * Get context protocol.
   *
   * @return A string view representing the protocol of the traceable stream behind the context.
   */
  virtual absl::string_view protocol() const PURE;

  /**
   * Get context host.
   *
   * @return The host of traceable stream. It typically should be domain, VIP, or service name that
   * used to represents target service instances.
   */
  virtual absl::string_view host() const PURE;

  /**
   * Get context path.
   *
   * @return The path of traceable stream. The content and meaning of path are determined by
   * specific protocol itself.
   */
  virtual absl::string_view path() const PURE;

  /**
   * Get context method.
   *
   * @return The method of traceable stream. The content and meaning of method are determined by
   * specific protocol itself.
   */
  virtual absl::string_view method() const PURE;

  /**
   * Iterate over all context entry.
   *
   * @param callback supplies the iteration callback.
   */
  virtual void forEach(IterateCallback callback) const PURE;

  /**
   * Get tracing context value by key.
   *
   * @param key The context key of string view type.
   * @return The optional context value of string_view type.
   */
  virtual absl::optional<absl::string_view> getByKey(absl::string_view key) const PURE;

  /**
   * Set new tracing context key/value pair.
   *
   * @param key The context key of string view type.
   * @param val The context value of string view type.
   */
  virtual void setByKey(absl::string_view key, absl::string_view val) PURE;

  /**
   * Set new tracing context key/value pair. The key MUST point to data that will live beyond
   * the lifetime of any traceable stream that using the string.
   *
   * @param key The context key of string view type.
   * @param val The context value of string view type.
   */
  virtual void setByReferenceKey(absl::string_view key, absl::string_view val) PURE;

  /**
   * Set new tracing context key/value pair. Both key and val MUST point to data that will live
   * beyond the lifetime of any traceable stream that using the string.
   *
   * @param key The context key of string view type.
   * @param val The context value of string view type.
   */
  virtual void setByReference(absl::string_view key, absl::string_view val) PURE;

  /**
   * Removes the following key and its associated values from the tracing
   * context.
   *
   * @param key The key to remove if it exists.
   */
  virtual void removeByKey(absl::string_view key) PURE;
};

} // namespace Tracing
} // namespace Envoy
