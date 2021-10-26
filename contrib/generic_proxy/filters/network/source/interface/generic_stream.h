#pragma once

#include <functional>
#include <memory>
#include <string>

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Proxy {
namespace NetworkFilters {
namespace GenericProxy {

class GenericStreamBase {
public:
  virtual ~GenericStreamBase();

  using IterateCallback = std::function<bool(absl::string_view key, absl::string_view val)>;

  /**
   * Iterate over all generic stream metadata entry.
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
   * Set new generic stream metadata key/value pair. Both key and val MUST point to data that will
   * live beyond the lifetime of any generic stream that using the string.
   *
   * @param key The metadata key of string view type.
   * @param val The metadata value of string view type.
   */
  virtual void setByReference(absl::string_view key, absl::string_view val) PURE;
};

class GenericRequest : public GenericStreamBase {
public:
  /**
   * Get request protocol.
   *
   * @return A string view representing the protocol of the generic request behind the context.
   */
  virtual absl::string_view protocol() const PURE;

  /**
   * Get request authority.
   *
   * @return The authority of generic request. It generally consists of the host and an optional
   * user information and an optional port.
   */
  virtual absl::string_view authority() const PURE;

  /**
   * Get request path.
   *
   * @return The path of generic request. The content and meaning of path are determined by
   * specific protocol itself.
   */
  virtual absl::string_view path() const PURE;

  /**
   * Get request method.
   *
   * @return The method of generic request. The content and meaning of method are determined by
   * specific protocol itself.
   */
  virtual absl::string_view method() const PURE;
};
using GenericRequestPtr = std::unique_ptr<GenericRequest>;
using GenericRequestSharedPtr = std::shared_ptr<GenericRequest>;

enum class GenericEvent {
  Timeout,
  ConnectionTimeout,
  ConnectionClosed,
  LocalConnectionClosed,
  ConnectionFailure,
};

enum class GenericState {
  NONE,
  OK,
  ExpectedError,
  UnknowedError,
  LocalOK,
  LocalExpectedError,
  LocalUnknowedError,
};

class GenericResponse : public GenericStreamBase {
public:
  /**
   * Get generic response protocol.
   *
   * @return A string view representing the protocol of the generic stream behind the context.
   */
  virtual absl::string_view protocol() const PURE;

  /**
   * Get generic response status.
   *
   * @return Generic response status.
   */
  virtual GenericState status() const PURE;

  /**
   * Get generic response status detail of string view type.
   *
   * @return Generic response status detail. Status detail is a specific supplement to status.
   */
  virtual absl::string_view statusDetail() const PURE;
};
using GenericResponsePtr = std::unique_ptr<GenericResponse>;
using GenericResponseSharedPtr = std::shared_ptr<GenericResponse>;

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Proxy
} // namespace Envoy
