#pragma once

#include "envoy/api/api.h"
#include "envoy/config/typed_config.h"
#include "envoy/network/connection.h"
#include "envoy/network/post_io_action.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/server/options.h"
#include "envoy/singleton/manager.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Ssl {

class HandshakeCallbacks {
public:
  virtual ~HandshakeCallbacks() = default;

  /**
   * @return the connection.
   */
  virtual Network::Connection& connection() const PURE;

  /**
   * A callback which will be executed at most once upon successful completion
   * of a handshake.
   */
  virtual void onSuccess(SSL* ssl) PURE;

  /**
   * A callback which will be executed at most once upon handshake failure.
   */
  virtual void onFailure() PURE;

  /**
   * Returns a pointer to the transportSocketCallbacks struct, or nullptr if
   * unset.
   */
  virtual Network::TransportSocketCallbacks* transportSocketCallbacks() PURE;

  /**
   * A callback to be called upon certificate validation completion if the validation is
   * asynchronous.
   */
  virtual void onAsynchronousCertValidationComplete() PURE;
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

// Callback for modifying an SSL_CTX.
using SslCtxCb = std::function<void(SSL_CTX*)>;

class HandshakerFactoryContext {
public:
  virtual ~HandshakerFactoryContext() = default;

  /**
   * Returns the singleton manager.
   */
  virtual Singleton::Manager& singletonManager() PURE;

  /**
   * @return reference to the server options
   */
  virtual const Server::Options& options() const PURE;

  /**
   * @return reference to the Api object
   */
  virtual Api::Api& api() PURE;

  /**
   * The list of supported protocols exposed via ALPN, from ContextConfig.
   */
  virtual absl::string_view alpnProtocols() const PURE;
};

struct HandshakerCapabilities {
  // Whether or not a handshaker implementation provides certificates itself.
  bool provides_certificates = false;

  // Whether or not a handshaker implementation verifies certificates itself.
  bool verifies_peer_certificates = false;

  // Whether or not a handshaker implementation handles session resumption
  // itself.
  bool handles_session_resumption = false;

  // Whether or not a handshaker implementation provides its own list of ciphers
  // and curves.
  bool provides_ciphers_and_curves = false;

  // Whether or not a handshaker implementation handles ALPN selection.
  bool handles_alpn_selection = false;

  // Should return true if this handshaker is FIPS-compliant.
  // Envoy will fail to compile if this returns true and `--define=boringssl=fips`.
  bool is_fips_compliant = true;
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

  /**
   * Implementations should return a struct with their capabilities. See
   * HandshakerCapabilities above. For any capability a Handshaker
   * implementation explicitly declares, Envoy will not also configure that SSL
   * capability.
   */
  virtual HandshakerCapabilities capabilities() const PURE;

  /**
   * Implementations should return a callback for configuring an SSL_CTX context
   * before it is used to create any SSL objects. Providing
   * |handshaker_factory_context| as an argument allows callsites to access the
   * API and other factory context methods.
   */
  virtual SslCtxCb sslctxCb(HandshakerFactoryContext& handshaker_factory_context) const PURE;
};

} // namespace Ssl
} // namespace Envoy
