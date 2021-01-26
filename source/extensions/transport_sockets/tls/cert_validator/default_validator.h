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

#include "common/common/matchers.h"
#include "common/stats/symbol_table_impl.h"

#include "extensions/transport_sockets/tls/cert_validator/cert_validator.h"
#include "extensions/transport_sockets/tls/stats.h"

#include "absl/synchronization/mutex.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class DefaultCertValidator : public CertValidator {
public:
  DefaultCertValidator(const Envoy::Ssl::CertificateValidationContextConfig* config,
                       SslStats& stats, TimeSource& time_source);

  ~DefaultCertValidator() override = default;

  // Tls::CertValidator
  void addClientValidationContext(SSL_CTX* context, bool require_client_cert) override;

  int doVerifyCertChain(X509_STORE_CTX* store_ctx, Ssl::SslExtendedSocketInfo* ssl_extended_info,
                        X509& leaf_cert,
                        const Network::TransportSocketOptions* transport_socket_options) override;

  int initializeSslContexts(std::vector<SSL_CTX*> contexts, bool provides_certificates) override;

  void updateDigestForSessionId(bssl::ScopedEVP_MD_CTX& md, uint8_t hash_buffer[EVP_MAX_MD_SIZE],
                                unsigned hash_length) override;

  size_t daysUntilFirstCertExpires() const override;
  std::string getCaFileName() const override { return ca_file_path_; };
  Envoy::Ssl::CertificateDetailsPtr getCaCertInformation() const override;

  // utility functions
  static int ignoreCertificateExpirationCallback(int ok, X509_STORE_CTX* store_ctx);

  Envoy::Ssl::ClientValidationStatus
  verifyCertificate(X509* cert, const std::vector<std::string>& verify_san_list,
                    const std::vector<Matchers::StringMatcherImpl>& subject_alt_name_matchers);

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
   * Determines whether the given name matches 'pattern' which may optionally begin with a wildcard.
   * NOTE:  public for testing
   * @param dns_name the DNS name to match
   * @param pattern the pattern to match against (*.example.com)
   * @return true if the san matches pattern
   */
  static bool dnsNameMatch(const absl::string_view dns_name, const absl::string_view pattern);

  /**
   * Performs subjectAltName matching with the provided matchers.
   * @param ssl the certificate to verify
   * @param subject_alt_name_matchers the configured matchers to match
   * @return true if the verification succeeds
   */
  static bool
  matchSubjectAltName(X509* cert,
                      const std::vector<Matchers::StringMatcherImpl>& subject_alt_name_matchers);

private:
  const Envoy::Ssl::CertificateValidationContextConfig* config_;
  SslStats& stats_;
  TimeSource& time_source_;

  bool allow_untrusted_certificate_{false};
  bssl::UniquePtr<X509> ca_cert_;
  std::string ca_file_path_;
  std::vector<Matchers::StringMatcherImpl> subject_alt_name_matchers_;
  std::vector<std::vector<uint8_t>> verify_certificate_hash_list_;
  std::vector<std::vector<uint8_t>> verify_certificate_spki_list_;
  std::vector<std::string> verify_subject_alt_name_list_;
  bool verify_trusted_ca_{false};
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
