#pragma once

#include "envoy/http/api_listener.h"

namespace Envoy {
namespace Server {

/**
 * Listener that allows consumer to interact with Envoy via a designated API.
 */
class ApiListener {
public:
  enum class Type { HttpApiListener };

  virtual ~ApiListener() = default;

  /**
   * An ApiListener is uniquely identified by its name.
   *
   * @return the name of the ApiListener.
   */
  virtual absl::string_view name() const PURE;

  /**
   * Shutdown the ApiListener. This is an interrupt, not a drain. In other words, calling this
   * function results in termination of all active streams vs. draining where no new streams are
   * allowed, but already existing streams are allowed to finish.
   */
  virtual void shutdown() PURE;

  /**
   * @return the Type of the ApiListener.
   */
  virtual Type type() const PURE;

  /**
   * @return valid ref IFF type() == Type::HttpApiListener, otherwise nullopt.
   */
  virtual Http::ApiListenerOptRef http() PURE;
};

using ApiListenerPtr = std::unique_ptr<ApiListener>;
using ApiListenerOptRef = absl::optional<std::reference_wrapper<ApiListener>>;

} // namespace Server
} // namespace Envoy