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
  const TlsCertificateConfig* tlsCertificate() const override {
    return tls_certficate_provider_ == nullptr ? nullptr : tls_certficate_provider_->secret();
  }
  const CertificateValidationContextConfig* certificateValidationContext() const override {
    return certficate_validation_context_provider_ == nullptr
               ? nullptr
               : certficate_validation_context_provider_->secret();
  }
  unsigned minProtocolVersion() const override { return min_protocol_version_; };
  unsigned maxProtocolVersion() const override { return max_protocol_version_; };

  bool isReady() const override {
    // Either tls_certficate_provider_ is nullptr or
    // tls_certficate_provider_->secret() is NOT nullptr and
    // either certficate_validation_context_provider_ is nullptr or
    // certficate_validation_context_provider_->secret() is NOT nullptr.
    return (!tls_certficate_provider_ || tls_certficate_provider_->secret() != nullptr) &&
           (!certficate_validation_context_provider_ ||
            certficate_validation_context_provider_->secret() != nullptr);
  }

  void setSecretUpdateCallback(std::function<void()> callback) override {
    if (tls_certficate_provider_) {
      if (tc_update_callback_handle_) {
        tc_update_callback_handle_->remove();
      }
      tc_update_callback_handle_ = tls_certficate_provider_->addUpdateCallback(callback);
    }
    if (certficate_validation_context_provider_) {
      if (cvc_update_callback_handle_) {
        cvc_update_callback_handle_->remove();
      }
      cvc_update_callback_handle_ =
          certficate_validation_context_provider_->addUpdateCallback(callback);
    }
  }

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
  Secret::TlsCertificateConfigProviderSharedPtr tls_certficate_provider_;
  // Handle for TLS certificate dyanmic secret callback.
  Common::CallbackHandle* tc_update_callback_handle_{};
  Secret::CertificateValidationContextConfigProviderSharedPtr
      certficate_validation_context_provider_;
  // Handle for certificate validation context dyanmic secret callback.
  Common::CallbackHandle* cvc_update_callback_handle_{};
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

private:
  const std::string server_name_indication_;
  const bool allow_renegotiation_;
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
