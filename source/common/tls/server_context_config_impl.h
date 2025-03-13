#pragma once

#include <string>
#include <vector>

#include "source/common/tls/context_config_impl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class ServerContextConfigImpl : public ContextConfigImpl, public Envoy::Ssl::ServerContextConfig {
public:
  static absl::StatusOr<std::unique_ptr<ServerContextConfigImpl>>
  create(const envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext& config,
         Server::Configuration::TransportSocketFactoryContext& secret_provider_context,
         bool for_quic);

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

  void setSecretUpdateCallback(std::function<absl::Status()> callback) override;
  bool disableStatelessSessionResumption() const override {
    return disable_stateless_session_resumption_;
  }
  bool disableStatefulSessionResumption() const override {
    return disable_stateful_session_resumption_;
  }

  bool fullScanCertsOnSNIMismatch() const override { return full_scan_certs_on_sni_mismatch_; }
  bool preferClientCiphers() const override { return prefer_client_ciphers_; }

  Ssl::TlsCertificateSelectorFactory tlsCertificateSelectorFactory() const override;

private:
  ServerContextConfigImpl(
      const envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext& config,
      Server::Configuration::TransportSocketFactoryContext& secret_provider_context,
      absl::Status& creation_status, bool for_quic);

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

  absl::StatusOr<std::vector<ServerContextConfig::SessionTicketKey>> getSessionTicketKeys(
      const envoy::extensions::transport_sockets::tls::v3::TlsSessionTicketKeys& keys);
  absl::StatusOr<ServerContextConfig::SessionTicketKey>
  getSessionTicketKey(const std::string& key_data);
  static OcspStaplePolicy ocspStaplePolicyFromProto(
      const envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext::OcspStaplePolicy&
          policy);

  Ssl::TlsCertificateSelectorFactory tls_certificate_selector_factory_;
  absl::optional<std::chrono::seconds> session_timeout_;
  const bool disable_stateless_session_resumption_;
  const bool disable_stateful_session_resumption_;
  bool full_scan_certs_on_sni_mismatch_;
  const bool prefer_client_ciphers_;
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
