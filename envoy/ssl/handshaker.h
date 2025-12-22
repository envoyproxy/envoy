#pragma once

#include "envoy/api/api.h"
#include "envoy/config/typed_config.h"
#include "envoy/network/connection.h"
#include "envoy/network/post_io_action.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/server/lifecycle_notifier.h"
#include "envoy/server/options.h"
#include "envoy/singleton/manager.h"

#include "absl/container/inlined_vector.h"
#include "openssl/ssl.h"

namespace Envoy {

namespace Server {
namespace Configuration {
class GenericFactoryContext;
} // namespace Configuration
} // namespace Server

namespace Ssl {

// Opaque type defined and used by the low-level TLS certificate context.
struct TlsContext;

class ServerContextConfig;

using CurveNID = int;
// Currently this type only ever holds 3 values: P-256, P-384, and P-521, so optimize by using
// `InlinedVector`.
using CurveNIDVector = absl::InlinedVector<int, 3>;

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

  /**
   * A callback to be called upon certificate selection completion if the selection is
   * asynchronous.
   */
  virtual void onAsynchronousCertificateSelectionComplete() PURE;
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

  /**
   * @return reference to the server lifecycle notifier
   */
  virtual Server::ServerLifecycleNotifier& lifecycleNotifier() PURE;
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

  // Whether or not a handshaker implementation provides its own list of
  // supported signature algorithms.
  bool provides_sigalgs = false;
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

// A handle tracking the certificate selection request. This can be used to supply additonal data
// to attach to the TLS sockets, and to detect when a request is cancelled.
class SelectionHandle {
public:
  virtual ~SelectionHandle() = default;
};

using SelectionHandleConstSharedPtr = std::shared_ptr<const SelectionHandle>;

struct SelectionResult {
  enum class SelectionStatus {
    // A certificate was successfully selected.
    Success,
    // Certificate selection will complete asynchronously later.
    Pending,
    // Certificate selection failed.
    Failed,
  };

  // Status of the certificate selection.
  SelectionStatus status;
  // Selected TLS context: it must be non-null when status is Success.
  const Ssl::TlsContext* selected_ctx{nullptr};
  // True if OCSP stapling should be enabled.
  bool staple{false};
  // Optional handle to attach to the individual TLS socket connection.
  SelectionHandleConstSharedPtr handle{nullptr};
};

/**
 * Used to return the result from an asynchronous cert selection.
 */
class CertificateSelectionCallback {
public:
  virtual ~CertificateSelectionCallback() = default;

  virtual Event::Dispatcher& dispatcher() PURE;

  /**
   * Called when the asynchronous cert selection completes.
   * @param selected_ctx selected Ssl::TlsContext, it's empty when selection failed.
   * @param staple true when need to set OCSP response.
   */
  virtual void onCertificateSelectionResult(OptRef<const Ssl::TlsContext> selected_ctx,
                                            bool staple) PURE;
};

using CertificateSelectionCallbackPtr = std::unique_ptr<CertificateSelectionCallback>;

enum class OcspStapleAction { Staple, NoStaple, Fail, ClientNotCapable };

class TlsCertificateSelector {
public:
  virtual ~TlsCertificateSelector() = default;

  /**
   * @return true if the selector provides its own SSL contexts.
   */
  virtual bool providesCertificates() const { return false; }

  /**
   * Select TLS context based on the client hello in non-QUIC TLS handshake.
   *
   * @return selected_ctx should only not be null when status is SelectionStatus::Success, and it
   * will have the same lifetime as ``ServerContextImpl``.
   *
   * @param ssl_client_hello low-level SSL object, only valid during the callback.
   */
  virtual SelectionResult selectTlsContext(const SSL_CLIENT_HELLO& ssl_client_hello,
                                           CertificateSelectionCallbackPtr cb) PURE;

  /**
   * Finds the best matching context in QUIC TLS handshake, which doesn't support async mode yet.
   *
   * @return context will have the same lifetime as ``ServerContextImpl``.
   */
  virtual std::pair<const Ssl::TlsContext&, OcspStapleAction>
  findTlsContext(absl::string_view sni, const CurveNIDVector& client_ecdsa_capabilities,
                 bool client_ocsp_capable, bool* cert_matched_sni) PURE;
};

using TlsCertificateSelectorPtr = std::unique_ptr<TlsCertificateSelector>;

class TlsCertificateSelectorContext {
public:
  virtual ~TlsCertificateSelectorContext() = default;

  /**
   * @return reference to the available TLS contexts.
   */
  virtual const std::vector<TlsContext>& getTlsContexts() const PURE;
};

class TlsCertificateSelectorFactory {
public:
  virtual ~TlsCertificateSelectorFactory() = default;

  /** Creates a per-context certificate selector.*/
  virtual TlsCertificateSelectorPtr create(TlsCertificateSelectorContext&) PURE;

  /** Notify about changes in the TLS context config, e.g. an SDS update to the certificates or the
   * validation context. */
  virtual absl::Status onConfigUpdate() PURE;
};

using TlsCertificateSelectorFactoryPtr = std::unique_ptr<TlsCertificateSelectorFactory>;

class TlsCertificateSelectorConfigFactory : public Config::TypedFactory {
public:
  /**
   * Create a certificate selector for a TLS context.
   * @param config proto configuration.
   * @param factory_context generic factory context.
   * @param tls_context is the parent TLS context which is guaranteed to outlive the selector.
   * @param for_quic true when in quic context, which does not support selecting certificate
   * asynchronously.
   * @returns a factory to create a TlsCertificateSelector. Accepts the |config| and
   * |validation_visitor| for early validation. This virtual base doesn't
   * perform MessageUtil::downcastAndValidate, but an implementation should.
   */
  virtual absl::StatusOr<TlsCertificateSelectorFactoryPtr>
  createTlsCertificateSelectorFactory(const Protobuf::Message& config,
                                      Server::Configuration::GenericFactoryContext& factory_context,
                                      const ServerContextConfig& tls_context, bool for_quic) PURE;

  std::string category() const override { return "envoy.tls.certificate_selectors"; }
};

using TlsCertificateMapper = std::function<std::string(const SSL_CLIENT_HELLO&)>;
using TlsCertificateMapperFactory = std::function<TlsCertificateMapper()>;

class TlsCertificateMapperConfigFactory : public Config::TypedFactory {
public:
  /**
   * Create a certificate selector secret name mapper.
   * @param config proto configuration.
   * @param factory_context generic factory context.
   */
  virtual absl::StatusOr<TlsCertificateMapperFactory> createTlsCertificateMapperFactory(
      const Protobuf::Message& config,
      Server::Configuration::GenericFactoryContext& factory_context) PURE;

  std::string category() const override { return "envoy.tls.certificate_mappers"; }
};

} // namespace Ssl
} // namespace Envoy
