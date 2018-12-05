#pragma once

#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "envoy/ssl/context.h"
#include "envoy/ssl/context_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "common/ssl/context_manager_impl.h"

#include "absl/synchronization/mutex.h"
#include "absl/types/optional.h"
#include "openssl/ssl.h"

namespace Envoy {
#ifndef OPENSSL_IS_BORINGSSL
#error Envoy requires BoringSSL
#endif

namespace Ssl {

// clang-format off
#define ALL_SSL_STATS(COUNTER, GAUGE, HISTOGRAM)                                                   \
  COUNTER(connection_error)                                                                        \
  COUNTER(handshake)                                                                               \
  COUNTER(session_reused)                                                                          \
  COUNTER(no_certificate)                                                                          \
  COUNTER(fail_verify_no_cert)                                                                     \
  COUNTER(fail_verify_error)                                                                       \
  COUNTER(fail_verify_san)                                                                         \
  COUNTER(fail_verify_cert_hash)
// clang-format on

/**
 * Wrapper struct for SSL stats. @see stats_macros.h
 */
struct SslStats {
  ALL_SSL_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

class ContextImpl : public virtual Context {
public:
  virtual bssl::UniquePtr<SSL> newSsl(absl::optional<std::string> override_server_name);

  /**
   * Logs successful TLS handshake and updates stats.
   * @param ssl the connection to log
   */
  void logHandshake(SSL* ssl) const;

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
   * @param san the subjectAltName to match
   * @param pattern the pattern to match against (*.example.com)
   * @return true if the san matches pattern
   */
  static bool dNSNameMatch(const std::string& dnsName, const char* pattern);

  SslStats& stats() { return stats_; }

  // Ssl::Context
  size_t daysUntilFirstCertExpires() const override;
  CertificateDetailsPtr getCaCertInformation() const override;
  std::vector<CertificateDetailsPtr> getCertChainInformation() const override;

protected:
  ContextImpl(Stats::Scope& scope, const ContextConfig& config, TimeSource& time_source);

  /**
   * The global SSL-library index used for storing a pointer to the context
   * in the SSL instance, for retrieval in callbacks.
   */
  static int sslContextIndex();

  // A X509_STORE_CTX_verify_cb callback for ignoring cert expiration in X509_verify_cert().
  static int ignoreCertificateExpirationCallback(int ok, X509_STORE_CTX* store_ctx);

  // A SSL_CTX_set_cert_verify_callback for custom cert validation.
  static int verifyCallback(X509_STORE_CTX* store_ctx, void* arg);

  int verifyCertificate(X509* cert);

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

  std::vector<uint8_t> parseAlpnProtocols(const std::string& alpn_protocols);
  static SslStats generateStats(Stats::Scope& scope);

  std::string getCaFileName() const { return ca_file_path_; };

  CertificateDetailsPtr certificateDetails(X509* cert, const std::string& path) const;

  struct TlsContext {
    // Each certificate specified for the context has its own SSL_CTX. SSL_CTXs
    // are identical with the exception of certificate material, and can be
    // safely substituted via SSL_set_SSL_CTX() during the
    // SSL_CTX_set_select_certificate_cb() callback following ClientHello.
    bssl::UniquePtr<SSL_CTX> ssl_ctx_;
    bssl::UniquePtr<X509> cert_chain_;
    std::string cert_chain_file_path_;
    bool is_ecdsa_{};

    std::string getCertChainFileName() const { return cert_chain_file_path_; };
    void addClientValidationContext(const CertificateValidationContextConfig& config,
                                    bool require_client_cert);
  };

  // This is always non-empty, with the first context used for all new SSL
  // objects. For server contexts, once we have ClientHello, we
  // potentially switch to a different CertificateContext based on certificate
  // selection.
  std::vector<TlsContext> tls_contexts_;
  bool verify_trusted_ca_{false};
  std::vector<std::string> verify_subject_alt_name_list_;
  std::vector<std::vector<uint8_t>> verify_certificate_hash_list_;
  std::vector<std::vector<uint8_t>> verify_certificate_spki_list_;
  Stats::Scope& scope_;
  SslStats stats_;
  std::vector<uint8_t> parsed_alpn_protocols_;
  bssl::UniquePtr<X509> ca_cert_;
  bssl::UniquePtr<X509> cert_chain_;
  std::string ca_file_path_;
  std::string cert_chain_file_path_;
  TimeSource& time_source_;
};

typedef std::shared_ptr<ContextImpl> ContextImplSharedPtr;

class ClientContextImpl : public ContextImpl, public ClientContext {
public:
  ClientContextImpl(Stats::Scope& scope, const ClientContextConfig& config,
                    TimeSource& time_source);

  bssl::UniquePtr<SSL> newSsl(absl::optional<std::string> override_server_name) override;

private:
  int newSessionKey(SSL_SESSION* session);

  const std::string server_name_indication_;
  const bool allow_renegotiation_;
  const size_t max_session_keys_;
  absl::Mutex session_keys_mu_;
  std::deque<bssl::UniquePtr<SSL_SESSION>> session_keys_ GUARDED_BY(session_keys_mu_);
  bool session_keys_single_use_{false};
};

class ServerContextImpl : public ContextImpl, public ServerContext {
public:
  ServerContextImpl(Stats::Scope& scope, const ServerContextConfig& config,
                    const std::vector<std::string>& server_names, TimeSource& time_source);

private:
  int alpnSelectCallback(const unsigned char** out, unsigned char* outlen, const unsigned char* in,
                         unsigned int inlen);
  int sessionTicketProcess(SSL* ssl, uint8_t* key_name, uint8_t* iv, EVP_CIPHER_CTX* ctx,
                           HMAC_CTX* hmac_ctx, int encrypt);
  // Select the TLS certificate context in SSL_CTX_set_select_certificate_cb() callback with
  // ClientHello details.
  enum ssl_select_cert_result_t selectTlsContext(const SSL_CLIENT_HELLO* ssl_client_hello);
  void generateHashForSessionContexId(const std::vector<std::string>& server_names,
                                      uint8_t* session_context_buf, unsigned& session_context_len);

  const std::vector<ServerContextConfig::SessionTicketKey> session_ticket_keys_;
};

} // namespace Ssl
} // namespace Envoy
