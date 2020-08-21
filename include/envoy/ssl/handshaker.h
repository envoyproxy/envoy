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

struct HandshakerRequirements {
  // Should be true if a handshaker requires certificates in the config.
  bool require_certificates = true;

  // Should be true if a handshaker requires that Envoy sets the session
  // resumption callback described by SSL_CTX_set_select_certificate_cb.
  bool should_set_select_certificate_cb = true;

  // Should be true if a handshaker requires that Envoy sets the ALPN selection
  // callback described by SSL_CTX_set_alpn_select_cb.
  bool should_set_alpn_select_cb = true;

  // Should be true if a handshaker requires that Envoy sets the TLS extension
  // ticket key callback described by SSL_CTX_set_tlsext_ticket_key_cb.
  bool should_set_tlsext_ticket_key_cb = true;

  // Should be true if a handshaker requires that Envoy sets the SSL timeout as
  // described by SSL_CTX_set_timeout.
  bool should_set_timeout = true;

  // Should be true if a handshaker requires that Envoy set the list of ciphers,
  // as described by SSL_CTX_set_strict_cipher_list.
  bool should_set_strict_cipher_list = true;

  // Should be true if a handshaker requires that Envoy set the list of curves,
  // as described by SSL_CTX_set1_curves_list.
  bool should_set1_curves_list = true;

  // Should be true if a handshaker requires that Envoy verify certificates.
  bool should_verify_certificates = true;

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
   * Implementations should return a struct with their requirements; see
   * HandshakerRequirements above.
   */
  virtual HandshakerRequirements requirements() const PURE;
};

} // namespace Ssl
} // namespace Envoy
