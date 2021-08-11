#pragma once

#include <string>
#include <vector>

#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/secret/secret_callbacks.h"
#include "envoy/secret/secret_provider.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/ssl/context_config.h"

#include "source/common/common/empty_string.h"
#include "source/common/json/json_loader.h"
#include "source/common/ssl/tls_certificate_config_impl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

static const std::string INLINE_STRING = "<inline>";

class ContextConfigImpl : public virtual Ssl::ContextConfig {
public:
  // Ssl::ContextConfig
  const std::string& alpnProtocols() const override { return alpn_protocols_; }
  const std::string& cipherSuites() const override { return cipher_suites_; }
  const std::string& ecdhCurves() const override { return ecdh_curves_; }
  // TODO(htuch): This needs to be made const again and/or zero copy and/or callers fixed.
  std::vector<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>>
  tlsCertificates() const override {
    std::vector<std::reference_wrapper<const Envoy::Ssl::TlsCertificateConfig>> configs;
    for (const auto& config : tls_certificate_configs_) {
      configs.push_back(config);
    }
    return configs;
  }
  const Envoy::Ssl::CertificateValidationContextConfig*
  certificateValidationContext() const override {
    return validation_context_config_.get();
  }
  unsigned minProtocolVersion() const override { return min_protocol_version_; };
  unsigned maxProtocolVersion() const override { return max_protocol_version_; };

  bool isReady() const override {
    const bool tls_is_ready =
        (tls_certificate_providers_.empty() || !tls_certificate_configs_.empty());
    const bool combined_cvc_is_ready =
        (default_cvc_ == nullptr || validation_context_config_ != nullptr);
    const bool cvc_is_ready = (certificate_validation_context_provider_ == nullptr ||
                               default_cvc_ != nullptr || validation_context_config_ != nullptr);
    return tls_is_ready && combined_cvc_is_ready && cvc_is_ready;
  }

  void setSecretUpdateCallback(std::function<void()> callback) override;
  Ssl::HandshakerFactoryCb createHandshaker() const override;
  Ssl::HandshakerCapabilities capabilities() const override { return capabilities_; }
  Ssl::SslCtxCb sslctxCb() const override { return sslctx_cb_; }

  Ssl::CertificateValidationContextConfigPtr getCombinedValidationContextConfig(
      const envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext&
          dynamic_cvc);

protected:
  ContextConfigImpl(const envoy::extensions::transport_sockets::tls::v3::CommonTlsContext& config,
                    const unsigned default_min_protocol_version,
                    const unsigned default_max_protocol_version,
                    const std::string& default_cipher_suites, const std::string& default_curves,
                    Server::Configuration::TransportSocketFactoryContext& factory_context);
  Api::Api& api_;

private:
  static unsigned tlsVersionFromProto(
      const envoy::extensions::transport_sockets::tls::v3::TlsParameters::TlsProtocol& version,
      unsigned default_version);

  const std::string alpn_protocols_;
  const std::string cipher_suites_;
  const std::string ecdh_curves_;

  std::vector<Ssl::TlsCertificateConfigImpl> tls_certificate_configs_;
  Ssl::CertificateValidationContextConfigPtr validation_context_config_;
  // If certificate validation context type is combined_validation_context. default_cvc_
  // holds a copy of CombinedCertificateValidationContext::default_validation_context.
  // Otherwise, default_cvc_ is nullptr.
  std::unique_ptr<envoy::extensions::transport_sockets::tls::v3::CertificateValidationContext>
      default_cvc_;
  std::vector<Secret::TlsCertificateConfigProviderSharedPtr> tls_certificate_providers_;
  // Handle for TLS certificate dynamic secret callback.
  std::vector<Envoy::Common::CallbackHandlePtr> tc_update_callback_handles_;
  Secret::CertificateValidationContextConfigProviderSharedPtr
      certificate_validation_context_provider_;
  // Handle for certificate validation context dynamic secret callback.
  Envoy::Common::CallbackHandlePtr cvc_update_callback_handle_;
  Envoy::Common::CallbackHandlePtr cvc_validation_callback_handle_;
  const unsigned min_protocol_version_;
  const unsigned max_protocol_version_;

  Ssl::HandshakerFactoryCb handshaker_factory_cb_;
  Ssl::HandshakerCapabilities capabilities_;
  Ssl::SslCtxCb sslctx_cb_;
};

class ClientContextConfigImpl : public ContextConfigImpl, public Envoy::Ssl::ClientContextConfig {
public:
  static const std::string DEFAULT_CIPHER_SUITES;
  static const std::string DEFAULT_CURVES;

  ClientContextConfigImpl(
      const envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext& config,
      absl::string_view sigalgs,
      Server::Configuration::TransportSocketFactoryContext& secret_provider_context);
  ClientContextConfigImpl(
      const envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext& config,
      Server::Configuration::TransportSocketFactoryContext& secret_provider_context)
      : ClientContextConfigImpl(config, "", secret_provider_context) {}

  // Ssl::ClientContextConfig
  const std::string& serverNameIndication() const override { return server_name_indication_; }
  bool allowRenegotiation() const override { return allow_renegotiation_; }
  size_t maxSessionKeys() const override { return max_session_keys_; }
  const std::string& signingAlgorithmsForTest() const override { return sigalgs_; }

private:
  static const unsigned DEFAULT_MIN_VERSION;
  static const unsigned DEFAULT_MAX_VERSION;

  const std::string server_name_indication_;
  const bool allow_renegotiation_;
  const size_t max_session_keys_;
  const std::string sigalgs_;
};

class ServerContextConfigImpl : public ContextConfigImpl, public Envoy::Ssl::ServerContextConfig {
public:
  ServerContextConfigImpl(
      const envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext& config,
      Server::Configuration::TransportSocketFactoryContext& secret_provider_context);

  // Ssl::ServerContextConfig
  bool requireClientCertificate() const override { return require_client_certificate_; }
  OcspStaplePolicy ocspStaplePolicy() const override { return ocsp_staple_policy_; }
  const std::vector<SessionTicketKey>& sessionTicketKeys() const override {
    return session_ticket_keys_;
  }
  absl::optional<std::chrono::seconds> sessionTimeout() const override { return session_timeout_; }

  bool isReady() const override {
    const bool parent_is_ready = ContextConfigImpl::isReady();
    const bool session_ticket_keys_are_ready =
        (session_ticket_keys_provider_ == nullptr || !session_ticket_keys_.empty());
    return parent_is_ready && session_ticket_keys_are_ready;
  }

  void setSecretUpdateCallback(std::function<void()> callback) override;
  bool disableStatelessSessionResumption() const override {
    return disable_stateless_session_resumption_;
  }

private:
  static const unsigned DEFAULT_MIN_VERSION;
  static const unsigned DEFAULT_MAX_VERSION;
  static const std::string DEFAULT_CIPHER_SUITES;
  static const std::string DEFAULT_CURVES;

  const bool require_client_certificate_;
  const OcspStaplePolicy ocsp_staple_policy_;
  std::vector<SessionTicketKey> session_ticket_keys_;
  const Secret::TlsSessionTicketKeysConfigProviderSharedPtr session_ticket_keys_provider_;
  Envoy::Common::CallbackHandlePtr stk_update_callback_handle_;
  Envoy::Common::CallbackHandlePtr stk_validation_callback_handle_;

  std::vector<ServerContextConfig::SessionTicketKey> getSessionTicketKeys(
      const envoy::extensions::transport_sockets::tls::v3::TlsSessionTicketKeys& keys);
  ServerContextConfig::SessionTicketKey getSessionTicketKey(const std::string& key_data);
  static OcspStaplePolicy ocspStaplePolicyFromProto(
      const envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext::OcspStaplePolicy&
          policy);

  absl::optional<std::chrono::seconds> session_timeout_;
  const bool disable_stateless_session_resumption_;
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
