#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/common/pure.h"
#include "envoy/network/transport_socket.h"
#include "envoy/registry/registry.h"
#include "envoy/ssl/context.h"
#include "envoy/ssl/context_config.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/ssl_socket_extended_info.h"

#include "source/common/common/logger.h"
#include "source/common/common/matchers.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/tls/cert_validator/cert_validator.h"
#include "source/common/tls/cert_validator/san_matcher.h"
#include "source/common/tls/stats.h"

#include "absl/synchronization/mutex.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class DefaultCertValidator : public CertValidator, Logger::Loggable<Logger::Id::connection> {
public:
  DefaultCertValidator(const Envoy::Ssl::CertificateValidationContextConfig* config,
                       SslStats& stats, Server::Configuration::CommonFactoryContext& context);

  ~DefaultCertValidator() override = default;

  // Tls::CertValidator
  absl::Status addClientValidationContext(SSL_CTX* context, bool require_client_cert) override;

  ValidationResults
  doVerifyCertChain(STACK_OF(X509)& cert_chain, Ssl::ValidateResultCallbackPtr callback,
                    const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
                    SSL_CTX& ssl, const CertValidator::ExtraValidationContext& validation_context,
                    bool is_server, absl::string_view host_name) override;

  absl::StatusOr<int> initializeSslContexts(std::vector<SSL_CTX*> contexts,
                                            bool provides_certificates) override;

  void updateDigestForSessionId(bssl::ScopedEVP_MD_CTX& md, uint8_t hash_buffer[EVP_MAX_MD_SIZE],
                                unsigned hash_length) override;

  absl::optional<uint32_t> daysUntilFirstCertExpires() const override;
  std::string getCaFileName() const override { return ca_file_path_; };
  Envoy::Ssl::CertificateDetailsPtr getCaCertInformation() const override;

  // Utility functions.
  Envoy::Ssl::ClientValidationStatus
  verifyCertificate(X509* cert, const std::vector<std::string>& verify_san_list,
                    const std::vector<SanMatcherPtr>& subject_alt_name_matchers,
                    std::string* error_details, uint8_t* out_alert);

  /**
   * Verifies certificate hash for pinning. The hash is a hex-encoded SHA-256 of the DER-encoded
   * certificate.
   *
   * @param ssl the certificate to verify
   * @param expected_hashes the configured list of certificate hashes to match
   * @return true if the verification succeeds
   */
  static bool verifyCertificateHashList(X509* cert,
                                        const std::vector<std::vector<uint8_t>>& expected_hashes);

  /**
   * Verifies certificate hash for pinning. The hash is a base64-encoded SHA-256 of the DER-encoded
   * Subject Public Key Information (SPKI) of the certificate.
   *
   * @param ssl the certificate to verify
   * @param expected_hashes the configured list of certificate hashes to match
   * @return true if the verification succeeds
   */
  static bool verifyCertificateSpkiList(X509* cert,
                                        const std::vector<std::vector<uint8_t>>& expected_hashes);

  /**
   * Performs subjectAltName verification
   * @param ssl the certificate to verify
   * @param subject_alt_names the configured subject_alt_names to match
   * @return true if the verification succeeds
   */
  static bool verifySubjectAltName(X509* cert, const std::vector<std::string>& subject_alt_names);

  /**
   * Performs subjectAltName matching with the provided matchers.
   * @param ssl the certificate to verify
   * @param subject_alt_name_matchers the configured matchers to match
   * @return true if the verification succeeds
   */
  static bool matchSubjectAltName(X509* cert,
                                  const std::vector<SanMatcherPtr>& subject_alt_name_matchers);

private:
  bool verifyCertAndUpdateStatus(X509* leaf_cert,
                                 const Network::TransportSocketOptions* transport_socket_options,
                                 Envoy::Ssl::ClientValidationStatus& detailed_status,
                                 std::string* error_details, uint8_t* out_alert);

  const Envoy::Ssl::CertificateValidationContextConfig* config_;
  SslStats& stats_;
  Server::Configuration::CommonFactoryContext& context_;

  bool allow_untrusted_certificate_{false};
  bssl::UniquePtr<X509> ca_cert_;
  std::string ca_file_path_;
  std::vector<SanMatcherPtr> subject_alt_name_matchers_;
  std::vector<std::vector<uint8_t>> verify_certificate_hash_list_;
  std::vector<std::vector<uint8_t>> verify_certificate_spki_list_;
  bool verify_trusted_ca_{false};
};

DECLARE_FACTORY(DefaultCertValidatorFactory);

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
