#pragma once

#include <openssl/safestack.h>

#include <array>
#include <deque>
#include <functional>
#include <memory>
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
#include "source/common/stats/symbol_table.h"
#include "source/common/tls/cert_validator/cert_validator.h"
#include "source/common/tls/context_manager_impl.h"
#include "source/common/tls/ocsp/ocsp.h"
#include "source/common/tls/stats.h"

#include "absl/synchronization/mutex.h"
#include "openssl/ssl.h"
#include "openssl/x509v3.h"

#ifdef ENVOY_ENABLE_QUIC
#include "quiche/quic/core/crypto/proof_source.h"
#endif

namespace Envoy {
#ifndef OPENSSL_IS_BORINGSSL
#error Envoy requires BoringSSL
#endif

namespace Ssl {

struct TlsContext {
  // Each certificate specified for the context has its own SSL_CTX. `SSL_CTXs`
  // are identical with the exception of certificate material, and can be
  // safely substituted via SSL_set_SSL_CTX() during the
  // SSL_CTX_set_select_certificate_cb() callback following ClientHello.
  bssl::UniquePtr<SSL_CTX> ssl_ctx_;
  bssl::UniquePtr<X509> cert_chain_;
  std::string cert_chain_file_path_;
  Extensions::TransportSockets::Tls::Ocsp::OcspResponseWrapperPtr ocsp_response_;
  bool is_ecdsa_{};
  bool is_must_staple_{};
  Ssl::PrivateKeyMethodProviderSharedPtr private_key_method_provider_{};

#ifdef ENVOY_ENABLE_QUIC
  quiche::QuicheReferenceCountedPointer<quic::ProofSource::Chain> quic_cert_;
  std::shared_ptr<quic::CertificatePrivateKey> quic_private_key_;
#endif

  std::string getCertChainFileName() const { return cert_chain_file_path_; };
  bool isCipherEnabled(uint16_t cipher_id, uint16_t client_version);
  Envoy::Ssl::PrivateKeyMethodProviderSharedPtr getPrivateKeyMethodProvider() {
    return private_key_method_provider_;
  }
  void loadCertificateChain(const std::string& data, const std::string& data_path);
  void loadPrivateKey(const std::string& data, const std::string& data_path,
                      const std::string& password);
  void loadPkcs12(const std::string& data, const std::string& data_path,
                  const std::string& password);
  void checkPrivateKey(const bssl::UniquePtr<EVP_PKEY>& pkey, const std::string& key_path);
};
} // namespace Ssl

namespace Extensions {
namespace TransportSockets {
namespace Tls {

class ContextImpl : public virtual Envoy::Ssl::Context,
                    protected Logger::Loggable<Logger::Id::config> {
public:
  virtual absl::StatusOr<bssl::UniquePtr<SSL>>
  newSsl(const Network::TransportSocketOptionsConstSharedPtr& options);

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

  static int sslSocketIndex();
  // Ssl::Context
  absl::optional<uint32_t> daysUntilFirstCertExpires() const override;
  Envoy::Ssl::CertificateDetailsPtr getCaCertInformation() const override;
  std::vector<Envoy::Ssl::CertificateDetailsPtr> getCertChainInformation() const override;
  absl::optional<uint64_t> secondsUntilFirstOcspResponseExpires() const override;

  std::vector<Ssl::PrivateKeyMethodProviderSharedPtr> getPrivateKeyMethodProviders();

  // Validate cert asynchronously for a QUIC connection.
  ValidationResults customVerifyCertChainForQuic(
      STACK_OF(X509)& cert_chain, Ssl::ValidateResultCallbackPtr callback, bool is_server,
      const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
      const CertValidator::ExtraValidationContext& validation_context,
      const std::string& host_name);

  static void keylogCallback(const SSL* ssl, const char* line);

protected:
  friend class ContextImplPeer;

  ContextImpl(Stats::Scope& scope, const Envoy::Ssl::ContextConfig& config,
              Server::Configuration::CommonFactoryContext& factory_context,
              Ssl::ContextAdditionalInitFunc additional_init);

  /**
   * The global SSL-library index used for storing a pointer to the context
   * in the SSL instance, for retrieval in callbacks.
   */
  static int sslContextIndex();

  // A SSL_CTX_set_custom_verify callback for asynchronous cert validation.
  static enum ssl_verify_result_t customVerifyCallback(SSL* ssl, uint8_t* out_alert);

  bool parseAndSetAlpn(const std::vector<std::string>& alpn, SSL& ssl);
  std::vector<uint8_t> parseAlpnProtocols(const std::string& alpn_protocols);

  void incCounter(const Stats::StatName name, absl::string_view value,
                  const Stats::StatName fallback) const;

  // Helper function to validate cert for TCP connections asynchronously.
  ValidationResults customVerifyCertChain(
      Envoy::Ssl::SslExtendedSocketInfo* extended_socket_info,
      const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options, SSL* ssl);

  void populateServerNamesMap(Ssl::TlsContext& ctx, const int pkey_id);

  // This is always non-empty, with the first context used for all new SSL
  // objects. For server contexts, once we have ClientHello, we
  // potentially switch to a different CertificateContext based on certificate
  // selection.
  std::vector<Ssl::TlsContext> tls_contexts_;
  CertValidatorPtr cert_validator_;
  Stats::Scope& scope_;
  SslStats stats_;
  std::vector<uint8_t> parsed_alpn_protocols_;
  bssl::UniquePtr<X509> cert_chain_;
  std::string cert_chain_file_path_;
  Server::Configuration::CommonFactoryContext& factory_context_;
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
  const Network::Address::IpList tls_keylog_local_;
  const Network::Address::IpList tls_keylog_remote_;
  AccessLog::AccessLogFileSharedPtr tls_keylog_file_;
};

using ContextImplSharedPtr = std::shared_ptr<ContextImpl>;

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
