#pragma once

#include "envoy/api/api.h"
#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/network/transport_socket.h"
#include "envoy/ssl/socket_state.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Ssl {

class HandshakerCallbacks {
public:
  virtual ~HandshakerCallbacks() = default;

  /**
   * Called when a handshake is successfully performed.
   */
  virtual void OnSuccessCb(SSL* ssl) PURE;
  /**
   * Called when a handshake fails.
   */
  virtual void OnFailureCb() PURE;
};

/*
 * Interface for a Handshaker which is responsible for owning the
 * bssl::UniquePtr<SSL> and performing handshakes.
 */
class Handshaker {
public:
  virtual ~Handshaker() = default;

  // The initialize method must be called before
  // doHandshake() is called.
  virtual void initialize(SSL& ssl) PURE;

  /**
   * Do the handshake.
   *
   *  * |state| is a mutable reference.
   *  * |callbacks| may not exist throughout the lifetime of the Handshaker, and
   *    should not be stored in an implementation.
   */
  virtual Network::PostIoAction doHandshake(SocketState& state,
                                            HandshakerCallbacks& callbacks) PURE;

  /**
   * Set an internal pointer to the Network::TransportSocketCallbacks struct.
   * Depending on impl, these callbacks can be invoked to access connection
   * state, raise connection events, etc.
   */
  virtual void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) PURE;

  /*
   * Access the held SSL object as a ptr. Callsites should handle nullptr
   * gracefully.
   */
  virtual SSL* ssl() PURE;
};

using HandshakerPtr = std::unique_ptr<Handshaker>;

class HandshakerFactoryContext {
public:
  virtual ~HandshakerFactoryContext() = default;

  /**
   * @return reference to the Api object
   */
  virtual Api::Api& api() PURE;

  /**
   * The list of supported protocols exposed via ALPN, from ContextConfig.
   */
  virtual absl::string_view alpnProtocols() const PURE;
};

class HandshakerFactory : public Config::TypedFactory {
public:
  virtual HandshakerPtr createHandshaker(bssl::UniquePtr<SSL> ssl,
                                         HandshakerFactoryContext& context) PURE;

  std::string category() const override { return "envoy.tls_handshakers"; }

  /**
   * Set a config for use when creating handshakers.
   */
  virtual void setConfig(ProtobufTypes::MessagePtr message) PURE;

  /**
   * Implementations should return true if the tls context accompanying this
   * handshaker expects certificates.
   */
  virtual bool requireCertificates() const PURE;
};

} // namespace Ssl
} // namespace Envoy
