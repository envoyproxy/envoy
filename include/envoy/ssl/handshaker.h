#pragma once

#include "envoy/api/api.h"
#include "envoy/config/typed_config.h"
#include "envoy/network/connection.h"
#include "envoy/network/post_io_action.h"
#include "envoy/protobuf/message_validator.h"

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

using HandshakerSharedPtr = std::shared_ptr<Handshaker>;
using HandshakerFactoryCb =
    std::function<HandshakerSharedPtr(bssl::UniquePtr<SSL>, int, HandshakeCallbacks*)>;

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
  /**
   * @returns a callback to create a Handshaker. Accepts the |config| and
   * |validation_visitor| for early validation. This virtual base doesn't
   * perform MessageUtil::downcastAndValidate, but an implementation should.
   */
  virtual HandshakerFactoryCb
  createHandshakerCb(const Protobuf::Message& message,
                     HandshakerFactoryContext& handshaker_factory_context,
                     ProtobufMessage::ValidationVisitor& validation_visitor) PURE;

  std::string category() const override { return "envoy.tls_handshakers"; }
};

} // namespace Ssl
} // namespace Envoy
