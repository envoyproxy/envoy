#pragma once

#include "envoy/network/connection.h"
#include "envoy/network/post_io_action.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Ssl {

class HandshakeCallbacks {
public:
  virtual ~HandshakeCallbacks() = default;

  /**
   * @return the connection state.
   */
  virtual Network::Connection::State connectionState() const PURE;

  /**
   * A callback which will be executed at most once upon successful completion
   * of a handshake.
   */
  virtual void onSuccess(SSL* ssl) PURE;

  /**
   * A callback which will be executed at most once upon handshake failure.
   */
  virtual void onFailure() PURE;
};

/**
 * Base interface for performing TLS handshakes.
 */
class Handshaker {
public:
  virtual ~Handshaker() = default;

  /**
   * Performs a TLS handshake and returns an action indicating
   * whether the callsite should close the connection or keep it open.
   */
  virtual Network::PostIoAction doHandshake() PURE;
};

} // namespace Ssl
} // namespace Envoy
