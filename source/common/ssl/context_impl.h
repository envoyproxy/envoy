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
#define ALL_SSL_STATS(COUNTER, GAUGE, TIMER)                                                       \
  COUNTER(connection_error)                                                                        \
  COUNTER(handshake)                                                                               \
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
  ALL_SSL_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_TIMER_STRUCT)
};

class ContextImpl : public virtual Context {
public:
  ~ContextImpl() { parent_.releaseContext(this); }

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
   * Determines whether the given DNS matches 'pattern' which may begin with a wildcard.
   * NOTE:  public for testing
   * @param dns_pattern the pattern to match against (*.example.com)
   * @param dns the DNS to match
   * @param dns_len length of DNS string
   * @return true if the dns matches pattern
   */
  static bool dNSNameMatch(const std::string& dns_pattern, const char* dns, size_t dns_len);

  /**
   * Determines whether the given URI matches 'pattern' which may end with a wildcard.
   * @param uri_pattern the pattern to match against
   * @param uri the URI to match
   * @param uri_len length of URI string
   * @return true if uri matches pattern
   */
  static bool uriMatch(const std::string& uri_pattern, const char* uri, size_t uri_len);

  SslStats& stats() { return stats_; }

  // Ssl::Context
  size_t daysUntilFirstCertExpires() override;
  std::string getCaCertInformation() override;
  std::string getCertChainInformation() override;

protected:
  ContextImpl(ContextManagerImpl& parent, Stats::Scope& scope, ContextConfig& config);

  /**
   * Specifies the context for which the session can be reused.  Any data is acceptable here.
   * @see SSL_CTX_set_session_id_ctx
   */
  static const unsigned char SERVER_SESSION_ID_CONTEXT;

  static int verifyCallback(X509_STORE_CTX* store_ctx, void* arg);
  int verifyCertificate(X509* cert);

  /**
   * Verifies certificate hash for pinning. The hash is the SHA-256 has of the DER encoding of the
   * certificate.
   *
   * The hash can be computed using 'openssl x509 -noout -fingerprint -sha256 -in cert.pem'
   *
   * @param ssl the certificate to verify
   * @param certificate_hash the configured certificate hash to match
   * @return true if the verification succeeds
   */
  static bool verifyCertificateHash(X509* cert, const std::vector<uint8_t>& certificate_hash);

  std::vector<uint8_t> parseAlpnProtocols(const std::string& alpn_protocols);
  static SslStats generateStats(Stats::Scope& scope);
  int32_t getDaysUntilExpiration(const X509* cert);
  bssl::UniquePtr<X509> loadCert(const std::string& cert_file);
  static std::string getSerialNumber(X509* cert);
  std::string getCaFileName() { return ca_file_path_; };
  std::string getCertChainFileName() { return cert_chain_file_path_; };

  ContextManagerImpl& parent_;
  bssl::UniquePtr<SSL_CTX> ctx_;
  std::vector<std::string> verify_subject_alt_name_list_;
  std::vector<uint8_t> verify_certificate_hash_;
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
  ClientContextImpl(ContextManagerImpl& parent, Stats::Scope& scope, ClientContextConfig& config);

  bssl::UniquePtr<SSL> newSsl() const override;

private:
  std::string server_name_indication_;
};

class ServerContextImpl : public ContextImpl, public ServerContext {
public:
  ServerContextImpl(ContextManagerImpl& parent, Stats::Scope& scope, ServerContextConfig& config,
                    Runtime::Loader& runtime);

private:
  int alpnSelectCallback(const unsigned char** out, unsigned char* outlen, const unsigned char* in,
                         unsigned int inlen);

  Runtime::Loader& runtime_;
  std::vector<uint8_t> parsed_alt_alpn_protocols_;
};

} // Ssl
} // Envoy
