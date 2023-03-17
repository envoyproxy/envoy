#pragma once

#include <functional>
#include <memory>
#include <string>

#include "envoy/common/pure.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

class StreamBase {
public:
  virtual ~StreamBase() = default;

  using IterateCallback = std::function<bool(absl::string_view key, absl::string_view val)>;

  /**
   * Get application protocol of generic stream.
   *
   * @return A string view representing the application protocol of the generic stream behind
   * the context.
   */
  virtual absl::string_view protocol() const PURE;

  /**
   * Iterate over all generic stream metadata entries.
   *
   * @param callback supplies the iteration callback.
   */
  virtual void forEach(IterateCallback callback) const PURE;

  /**
   * Get generic stream metadata value by key.
   *
   * @param key The metadata key of string view type.
   * @return The optional metadata value of string_view type.
   */
  virtual absl::optional<absl::string_view> getByKey(absl::string_view key) const PURE;

  /**
   * Set new generic stream metadata key/value pair.
   *
   * @param key The metadata key of string view type.
   * @param val The metadata value of string view type.
   */
  virtual void setByKey(absl::string_view key, absl::string_view val) PURE;

  /**
   * Set new generic stream metadata key/value pair. The key MUST point to data that will live
   * beyond the lifetime of any generic stream that using the string.
   *
   * @param key The metadata key of string view type.
   * @param val The metadata value of string view type.
   */
  virtual void setByReferenceKey(absl::string_view key, absl::string_view val) PURE;

  /**
   * Set new generic stream metadata key/value pair. Both key and val MUST point to data that
   * will live beyond the lifetime of any generic stream that using the string.
   *
   * @param key The metadata key of string view type.
   * @param val The metadata value of string view type.
   */
  virtual void setByReference(absl::string_view key, absl::string_view val) PURE;

  // Used for matcher.
  static constexpr absl::string_view name() { return "generic_proxy"; }
};

class Request : public StreamBase {
public:
  /**
   * Get request host.
   *
   * @return The host of generic request. The meaning of the return value may be different For
   * different application protocols. It typically should be domain, VIP, or service name that
   * used to represents target service instances.
   */
  virtual absl::string_view host() const PURE;

  /**
   * Get request path.
   *
   * @return The path of generic request. The meaning of the return value may be different For
   * different application protocols. It typically should be RPC service name that used to
   * represents set of method or functionality provided by target service.
   */
  virtual absl::string_view path() const PURE;

  /**
   * Get request method.
   *
   * @return The method of generic request. The meaning of the return value may be different For
   * different application protocols.
   */
  virtual absl::string_view method() const PURE;
};
using RequestPtr = std::unique_ptr<Request>;
using RequestSharedPtr = std::shared_ptr<Request>;

enum class Event {
  Timeout,
  ConnectionTimeout,
  ConnectionClosed,
  LocalConnectionClosed,
  ConnectionFailure,
};

using Status = absl::Status;
using StatusCode = absl::StatusCode;

class Response : public StreamBase {
public:
  /**
   * Get response status.
   *
   * @return generic response status.
   */
  virtual Status status() const PURE;
};

using ResponsePtr = std::unique_ptr<Response>;
using ResponseSharedPtr = std::shared_ptr<Response>;

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
