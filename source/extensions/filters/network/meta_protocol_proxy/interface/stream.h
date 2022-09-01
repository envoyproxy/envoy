#pragma once

#include <functional>
#include <memory>
#include <string>

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetaProtocolProxy {

class StreamBase {
public:
  virtual ~StreamBase() = default;

  using IterateCallback = std::function<bool(absl::string_view key, absl::string_view val)>;

  /**
   * Get application protocol of meta protocol stream.
   *
   * @return A string view representing the application protocol of the meta protocol stream behind
   * the context.
   */
  virtual absl::string_view protocol() const PURE;

  /**
   * Iterate over all meta protocol stream metadata entries.
   *
   * @param callback supplies the iteration callback.
   */
  virtual void forEach(IterateCallback callback) const PURE;

  /**
   * Get meta protocol stream metadata value by key.
   *
   * @param key The metadata key of string view type.
   * @return The optional metadata value of string_view type.
   */
  virtual absl::optional<absl::string_view> getByKey(absl::string_view key) const PURE;

  /**
   * Set new meta protocol stream metadata key/value pair.
   *
   * @param key The metadata key of string view type.
   * @param val The metadata value of string view type.
   */
  virtual void setByKey(absl::string_view key, absl::string_view val) PURE;

  /**
   * Set new meta protocol stream metadata key/value pair. The key MUST point to data that will live
   * beyond the lifetime of any meta protocol stream that using the string.
   *
   * @param key The metadata key of string view type.
   * @param val The metadata value of string view type.
   */
  virtual void setByReferenceKey(absl::string_view key, absl::string_view val) PURE;

  /**
   * Set new meta protocol stream metadata key/value pair. Both key and val MUST point to data that
   * will live beyond the lifetime of any meta protocol stream that using the string.
   *
   * @param key The metadata key of string view type.
   * @param val The metadata value of string view type.
   */
  virtual void setByReference(absl::string_view key, absl::string_view val) PURE;
};

class Request : public StreamBase {
public:
  /**
   * Get request host.
   *
   * @return The host of meta protocol request. It generally consists of the host and an
   * optional user information and an optional port. For different application protocols, the
   * meaning of the return value may be different.
   */
  virtual absl::string_view host() const PURE;

  /**
   * Get request path.
   *
   * @return The path of meta protocol request. The content and meaning of path are determined by
   * specific protocol itself.
   */
  virtual absl::string_view path() const PURE;

  /**
   * Get request method.
   *
   * @return The method of meta protocol request. The content and meaning of method are determined
   * by specific protocol itself.
   */
  virtual absl::string_view method() const PURE;

  static constexpr absl::string_view name() { return "meta_protocol"; }
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
   * @return meta protocol response status.
   */
  virtual Status status() const PURE;
};

using ResponsePtr = std::unique_ptr<Response>;
using ResponseSharedPtr = std::shared_ptr<Response>;

} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
