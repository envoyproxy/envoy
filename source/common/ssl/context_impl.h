#pragma once

#include <string>
#include <vector>

#include "envoy/runtime/runtime.h"
#include "envoy/ssl/context.h"
#include "envoy/ssl/context_config.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/stats_macros.h"

#include "common/ssl/context_impl.h"
#include "common/ssl/context_manager_impl.h"

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
  virtual bssl::UniquePtr<SSL> newSsl() const;

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
  std::string getCaCertInformation() const override;
  std::string getCertChainInformation() const override;

protected:
  ContextImpl(ContextManagerImpl& parent, Stats::Scope& scope, const ContextConfig& config);
  ~ContextImpl() { parent_.releaseContext(this); }

  /**
   * The global SSL-library index used for storing a pointer to the context
   * in the SSL instance, for retrieval in callbacks.
   */
  static int sslContextIndex();

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
  int32_t getDaysUntilExpiration(const X509* cert) const;
  static std::string getSerialNumber(const X509* cert);
  std::string getCaFileName() const { return ca_file_path_; };
  std::string getCertChainFileName() const { return cert_chain_file_path_; };

  ContextManagerImpl& parent_;
  bssl::UniquePtr<SSL_CTX> ctx_;
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
};

class ClientContextImpl : public ContextImpl, public ClientContext {
public:
  ClientContextImpl(ContextManagerImpl& parent, Stats::Scope& scope,
                    const ClientContextConfig& config);

  bssl::UniquePtr<SSL> newSsl() const override;

private:
  const std::string server_name_indication_;
  const bool allow_renegotiation_;
};

class ServerContextImpl : public ContextImpl, public ServerContext {
public:
  ServerContextImpl(ContextManagerImpl& parent, Stats::Scope& scope,
                    const ServerContextConfig& config, const std::vector<std::string>& server_names,
                    Runtime::Loader& runtime);

private:
  int alpnSelectCallback(const unsigned char** out, unsigned char* outlen, const unsigned char* in,
                         unsigned int inlen);
  int sessionTicketProcess(SSL* ssl, uint8_t* key_name, uint8_t* iv, EVP_CIPHER_CTX* ctx,
                           HMAC_CTX* hmac_ctx, int encrypt);

  Runtime::Loader& runtime_;
  std::vector<uint8_t> parsed_alt_alpn_protocols_;
  const std::vector<ServerContextConfig::SessionTicketKey> session_ticket_keys_;
};

} // namespace Ssl
} // namespace Envoy
