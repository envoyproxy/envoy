#pragma once

#include <array>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/network/transport_socket.h"
#include "envoy/ssl/context.h"
#include "envoy/ssl/context_config.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/ssl_socket_extended_info.h"

#include "source/common/common/matchers.h"
#include "source/common/stats/symbol_table.h"
#include "source/extensions/transport_sockets/tls/stats.h"

#include "openssl/ssl.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

struct ValidationResults {
  enum class ValidationStatus {
    Pending,
    Successful,
    Failed,
  };
  // If the value is Pending, the validation is asynchronous.
  // If the value is Failed, refer to tls_alert and error_details for detailed error messages.
  ValidationStatus status;
  // Detailed status of the underlying validation. Depending on the validation configuration,
  // `status` may be valid but `detailed_status` might not be.
  Envoy::Ssl::ClientValidationStatus detailed_status;
  // The TLS alert used to interpret validation error if the validation failed.
  absl::optional<uint8_t> tls_alert;
  // The detailed error messages populated during validation.
  absl::optional<std::string> error_details;
};

class CertValidator {
public:
  // Wraps cert validation parameters added from time to time.
  struct ExtraValidationContext {
    // The pointer to transport socket callbacks.
    Network::TransportSocketCallbacks* callbacks;
  };

  virtual ~CertValidator() = default;

  /**
   * Called to add the client validation context information to a given ssl context
   *
   * @param context the store context
   * @param require_client_cert whether or not client cert is required
   */
  virtual void addClientValidationContext(SSL_CTX* context, bool require_client_cert) PURE;

  /**
   * Called by customVerifyCallback to do the actual cert chain verification which could be
   * asynchronous. If the verification is asynchronous, Pending will be returned. After the
   * asynchronous verification is finished, the result should be passed back via a
   * VerifyResultCallback object.
   * @param cert_chain the cert chain with the leaf cert on top.
   * @param callback called after the asynchronous validation finishes to handle the result. Must
   * outlive this call if it returns Pending. Not used if doing synchronous verification. If not
   * provided and the validation is asynchronous, ssl_extended_info will create one.
   * @param transport_socket_options config options to validate cert, might short live the
   * validation if it is asynchronous.
   * @param ssl_ctx the config context this validation should use.
   * @param validation_context a placeholder for additional validation parameters.
   * @param is_server whether the validation is on server side.
   * @return ValidationResult the validation status and error messages if there is any.
   */
  virtual ValidationResults
  doVerifyCertChain(STACK_OF(X509)& cert_chain, Ssl::ValidateResultCallbackPtr callback,
                    const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                    SSL_CTX& ssl_ctx, const ExtraValidationContext& validation_context,
                    bool is_server, absl::string_view host_name) PURE;

  /**
   * Called to initialize all ssl contexts
   *
   * @param contexts the store context
   * @param handshaker_provides_certificates whether or not a handshaker implementation provides
   * certificates itself.
   * @return the ssl verification mode flag
   */
  virtual int initializeSslContexts(std::vector<SSL_CTX*> contexts,
                                    bool handshaker_provides_certificates) PURE;

  /**
   * Called when calculation hash for session context ids. This hash MUST include all
   * configuration used to validate a peer certificate, so that if this configuration
   * is changed, sessions cannot be re-used and must be re-negotiated and re-validated
   * using the new settings.
   *
   * @param md the store context
   * @param hash_buffer the buffer used for digest calculation
   * @param hash_length the expected length of hash
   */
  virtual void updateDigestForSessionId(bssl::ScopedEVP_MD_CTX& md,
                                        uint8_t hash_buffer[EVP_MAX_MD_SIZE],
                                        unsigned hash_length) PURE;

  virtual absl::optional<uint32_t> daysUntilFirstCertExpires() const PURE;
  virtual std::string getCaFileName() const PURE;
  virtual Envoy::Ssl::CertificateDetailsPtr getCaCertInformation() const PURE;
};

using CertValidatorPtr = std::unique_ptr<CertValidator>;

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
