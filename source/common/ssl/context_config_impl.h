#pragma once

#include <string>
#include <vector>

#include "envoy/api/v2/auth/cert.pb.h"
#include "envoy/secret/secret_callbacks.h"
#include "envoy/secret/secret_provider.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/context_config.h"

#include "common/common/empty_string.h"
#include "common/json/json_loader.h"
#include "common/ssl/tls_certificate_config_impl.h"

namespace Envoy {
namespace Ssl {

static const std::string INLINE_STRING = "<inline>";

class ContextConfigImpl : public virtual Ssl::ContextConfig {
public:
  ~ContextConfigImpl() override;

  // Ssl::ContextConfig
  const std::string& alpnProtocols() const override { return alpn_protocols_; }
  const std::string& cipherSuites() const override { return cipher_suites_; }
  const std::string& ecdhCurves() const override { return ecdh_curves_; }
  // TODO(htuch): This needs to be made const again and/or zero copy and/or callers fixed.
  std::vector<std::reference_wrapper<const TlsCertificateConfig>> tlsCertificates() const override {
    std::vector<std::reference_wrapper<const TlsCertificateConfig>> configs;
    for (const auto& config : tls_certificate_configs_) {
      configs.push_back(config);
    }
    return configs;
  }
  const CertificateValidationContextConfig* certificateValidationContext() const override {
    return validation_context_config_.get();
  }
  unsigned minProtocolVersion() const override { return min_protocol_version_; };
  unsigned maxProtocolVersion() const override { return max_protocol_version_; };

  bool isReady() const override {
    const bool tls_is_ready =
        (tls_certficate_providers_.empty() || !tls_certificate_configs_.empty());
    const bool combined_cvc_is_ready =
        (default_cvc_ == nullptr || validation_context_config_ != nullptr);
    const bool cvc_is_ready = (certficate_validation_context_provider_ == nullptr ||
                               default_cvc_ != nullptr || validation_context_config_ != nullptr);
    return tls_is_ready && combined_cvc_is_ready && cvc_is_ready;
  }

  void setSecretUpdateCallback(std::function<void()> callback) override;

  Ssl::CertificateValidationContextConfigPtr getCombinedValidationContextConfig(
      const envoy::api::v2::auth::CertificateValidationContext& dynamic_cvc);

protected:
  ContextConfigImpl(const envoy::api::v2::auth::CommonTlsContext& config,
                    Server::Configuration::TransportSocketFactoryContext& factory_context);

private:
  static unsigned
  tlsVersionFromProto(const envoy::api::v2::auth::TlsParameters_TlsProtocol& version,
                      unsigned default_version);

  static const std::string DEFAULT_CIPHER_SUITES;
  static const std::string DEFAULT_ECDH_CURVES;

  const std::string alpn_protocols_;
  const std::string cipher_suites_;
  const std::string ecdh_curves_;

  std::vector<Ssl::TlsCertificateConfigImpl> tls_certificate_configs_;
  Ssl::CertificateValidationContextConfigPtr validation_context_config_;
  // If certificate validation context type is combined_validation_context. default_cvc_
  // holds a copy of CombinedCertificateValidationContext::default_validation_context.
  // Otherwise, default_cvc_ is nullptr.
  std::unique_ptr<envoy::api::v2::auth::CertificateValidationContext> default_cvc_;
  std::vector<Secret::TlsCertificateConfigProviderSharedPtr> tls_certficate_providers_;
  // Handle for TLS certificate dyanmic secret callback.
  Common::CallbackHandle* tc_update_callback_handle_{};
  Secret::CertificateValidationContextConfigProviderSharedPtr
      certficate_validation_context_provider_;
  // Handle for certificate validation context dyanmic secret callback.
  Common::CallbackHandle* cvc_update_callback_handle_{};
  Common::CallbackHandle* cvc_validation_callback_handle_{};
  const unsigned min_protocol_version_;
  const unsigned max_protocol_version_;
};

class ClientContextConfigImpl : public ContextConfigImpl, public ClientContextConfig {
public:
  explicit ClientContextConfigImpl(
      const envoy::api::v2::auth::UpstreamTlsContext& config,
      Server::Configuration::TransportSocketFactoryContext& secret_provider_context);
  explicit ClientContextConfigImpl(
      const Json::Object& config,
      Server::Configuration::TransportSocketFactoryContext& secret_provider_context);

  // Ssl::ClientContextConfig
  const std::string& serverNameIndication() const override { return server_name_indication_; }
  bool allowRenegotiation() const override { return allow_renegotiation_; }
  size_t maxSessionKeys() const override { return max_session_keys_; }

private:
  const std::string server_name_indication_;
  const bool allow_renegotiation_;
  const size_t max_session_keys_;
};

class ServerContextConfigImpl : public ContextConfigImpl, public ServerContextConfig {
public:
  explicit ServerContextConfigImpl(
      const envoy::api::v2::auth::DownstreamTlsContext& config,
      Server::Configuration::TransportSocketFactoryContext& secret_provider_context);
  explicit ServerContextConfigImpl(
      const Json::Object& config,
      Server::Configuration::TransportSocketFactoryContext& secret_provider_context);

  // Ssl::ServerContextConfig
  bool requireClientCertificate() const override { return require_client_certificate_; }
  const std::vector<SessionTicketKey>& sessionTicketKeys() const override {
    return session_ticket_keys_;
  }

private:
  const bool require_client_certificate_;
  const std::vector<SessionTicketKey> session_ticket_keys_;

  static void validateAndAppendKey(std::vector<ServerContextConfig::SessionTicketKey>& keys,
                                   const std::string& key_data);
};

} // namespace Ssl
} // namespace Envoy
