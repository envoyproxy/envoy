#pragma once

#include <array>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "envoy/network/transport_socket.h"
#include "envoy/ssl/context.h"
#include "envoy/ssl/context_config.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/ssl_socket_extended_info.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/matchers.h"
#include "source/common/stats/symbol_table_impl.h"
#include "source/extensions/transport_sockets/tls/cert_validator/cert_validator.h"
#include "source/extensions/transport_sockets/tls/context_manager_impl.h"
#include "source/extensions/transport_sockets/tls/ocsp/ocsp.h"
#include "source/extensions/transport_sockets/tls/stats.h"

#include "absl/synchronization/mutex.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

namespace Envoy {
#ifndef OPENSSL_IS_BORINGSSL
#error Envoy requires BoringSSL
#endif

namespace Extensions {
namespace TransportSockets {
namespace Tls {

struct TlsContext {
  // Each certificate specified for the context has its own SSL_CTX. `SSL_CTXs`
  // are identical with the exception of certificate material, and can be
  // safely substituted via SSL_set_SSL_CTX() during the
  // SSL_CTX_set_select_certificate_cb() callback following ClientHello.
  bssl::UniquePtr<SSL_CTX> ssl_ctx_;
  bssl::UniquePtr<X509> cert_chain_;
  std::string cert_chain_file_path_;
  Ocsp::OcspResponseWrapperPtr ocsp_response_;
  bool is_ecdsa_{};
  bool is_must_staple_{};
  Ssl::PrivateKeyMethodProviderSharedPtr private_key_method_provider_{};

  std::string getCertChainFileName() const { return cert_chain_file_path_; };
  bool isCipherEnabled(uint16_t cipher_id, uint16_t client_version);
  Envoy::Ssl::PrivateKeyMethodProviderSharedPtr getPrivateKeyMethodProvider() {
    return private_key_method_provider_;
  }
};

class ContextImpl : public virtual Envoy::Ssl::Context {
public:
  virtual bssl::UniquePtr<SSL> newSsl(const Network::TransportSocketOptions* options);

  /**
   * Logs successful TLS handshake and updates stats.
   * @param ssl the connection to log
   */
  void logHandshake(SSL* ssl) const;

  SslStats& stats() { return stats_; }

  /**
   * The global SSL-library index used for storing a pointer to the SslExtendedSocketInfo
   * class in the SSL instance, for retrieval in callbacks.
   */
  static int sslExtendedSocketInfoIndex();

  // Ssl::Context
  size_t daysUntilFirstCertExpires() const override;
  Envoy::Ssl::CertificateDetailsPtr getCaCertInformation() const override;
  std::vector<Envoy::Ssl::CertificateDetailsPtr> getCertChainInformation() const override;
  absl::optional<uint64_t> secondsUntilFirstOcspResponseExpires() const override;

  std::vector<Ssl::PrivateKeyMethodProviderSharedPtr> getPrivateKeyMethodProviders();

  bool verifyCertChain(X509& leaf_cert, STACK_OF(X509) & intermediates, std::string& error_details);

protected:
  ContextImpl(Stats::Scope& scope, const Envoy::Ssl::ContextConfig& config,
              TimeSource& time_source);

  /**
   * The global SSL-library index used for storing a pointer to the context
   * in the SSL instance, for retrieval in callbacks.
   */
  static int sslContextIndex();

  // A SSL_CTX_set_cert_verify_callback for custom cert validation.
  static int verifyCallback(X509_STORE_CTX* store_ctx, void* arg);

  bool parseAndSetAlpn(const std::vector<std::string>& alpn, SSL& ssl);
  std::vector<uint8_t> parseAlpnProtocols(const std::string& alpn_protocols);

  void incCounter(const Stats::StatName name, absl::string_view value,
                  const Stats::StatName fallback) const;

  // This is always non-empty, with the first context used for all new SSL
  // objects. For server contexts, once we have ClientHello, we
  // potentially switch to a different CertificateContext based on certificate
  // selection.
  std::vector<TlsContext> tls_contexts_;
  CertValidatorPtr cert_validator_;
  Stats::Scope& scope_;
  SslStats stats_;
  std::vector<uint8_t> parsed_alpn_protocols_;
  bssl::UniquePtr<X509> cert_chain_;
  std::string cert_chain_file_path_;
  TimeSource& time_source_;
  const unsigned tls_max_version_;
  mutable Stats::StatNameSetPtr stat_name_set_;
  const Stats::StatName unknown_ssl_cipher_;
  const Stats::StatName unknown_ssl_curve_;
  const Stats::StatName unknown_ssl_algorithm_;
  const Stats::StatName unknown_ssl_version_;
  const Stats::StatName ssl_ciphers_;
  const Stats::StatName ssl_versions_;
  const Stats::StatName ssl_curves_;
  const Stats::StatName ssl_sigalgs_;
  const Ssl::HandshakerCapabilities capabilities_;
};

using ContextImplSharedPtr = std::shared_ptr<ContextImpl>;

class ClientContextImpl : public ContextImpl, public Envoy::Ssl::ClientContext {
public:
  ClientContextImpl(Stats::Scope& scope, const Envoy::Ssl::ClientContextConfig& config,
                    TimeSource& time_source);

  bssl::UniquePtr<SSL> newSsl(const Network::TransportSocketOptions* options) override;

private:
  int newSessionKey(SSL_SESSION* session);
  uint16_t parseSigningAlgorithmsForTest(const std::string& sigalgs);

  const std::string server_name_indication_;
  const bool allow_renegotiation_;
  const size_t max_session_keys_;
  absl::Mutex session_keys_mu_;
  std::deque<bssl::UniquePtr<SSL_SESSION>> session_keys_ ABSL_GUARDED_BY(session_keys_mu_);
  bool session_keys_single_use_{false};
};

enum class OcspStapleAction { Staple, NoStaple, Fail, ClientNotCapable };

class ServerContextImpl : public ContextImpl, public Envoy::Ssl::ServerContext {
public:
  ServerContextImpl(Stats::Scope& scope, const Envoy::Ssl::ServerContextConfig& config,
                    const std::vector<std::string>& server_names, TimeSource& time_source);

  // Select the TLS certificate context in SSL_CTX_set_select_certificate_cb() callback with
  // ClientHello details. This is made public for use by custom TLS extensions who want to
  // manually create and use this as a client hello callback.
  enum ssl_select_cert_result_t selectTlsContext(const SSL_CLIENT_HELLO* ssl_client_hello);

private:
  using SessionContextID = std::array<uint8_t, SSL_MAX_SSL_SESSION_ID_LENGTH>;

  int alpnSelectCallback(const unsigned char** out, unsigned char* outlen, const unsigned char* in,
                         unsigned int inlen);
  int sessionTicketProcess(SSL* ssl, uint8_t* key_name, uint8_t* iv, EVP_CIPHER_CTX* ctx,
                           HMAC_CTX* hmac_ctx, int encrypt);
  bool isClientEcdsaCapable(const SSL_CLIENT_HELLO* ssl_client_hello);
  bool isClientOcspCapable(const SSL_CLIENT_HELLO* ssl_client_hello);
  OcspStapleAction ocspStapleAction(const TlsContext& ctx, bool client_ocsp_capable);

  SessionContextID generateHashForSessionContextId(const std::vector<std::string>& server_names);

  const std::vector<Envoy::Ssl::ServerContextConfig::SessionTicketKey> session_ticket_keys_;
  const Ssl::ServerContextConfig::OcspStaplePolicy ocsp_staple_policy_;
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
