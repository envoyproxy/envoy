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
   * @return the Type of the ApiListener.
   */
  virtual Type type() const PURE;

  /**
   * Create an Http::ApiListener capable of starting synthetic HTTP streams. The returned listener
   * must only be deleted in the dispatcher's thread.
   *
   * @return valid pointer IFF type() == Type::HttpApiListener, otherwise nullptr.
   */
  virtual Http::ApiListenerPtr createHttpApiListener(Event::Dispatcher& dispatcher) PURE;
};

using ApiListenerPtr = std::unique_ptr<ApiListener>;
using ApiListenerOptRef = absl::optional<std::reference_wrapper<ApiListener>>;

} // namespace Server
} // namespace Envoy
