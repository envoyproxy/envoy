#pragma once

#include "envoy/api/api.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/network/address.h"
#include "envoy/network/transport_socket.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/ssl/context_manager.h"

#include "source/common/tls/context_impl.h"

namespace Envoy {
namespace Ssl {

struct ClientSslTransportOptions {
  ClientSslTransportOptions& setAlpn(bool alpn) {
    alpn_ = alpn;
    return *this;
  }

  ClientSslTransportOptions& setSan(absl::string_view san) {
    san_ = std::string(san);
    return *this;
  }

  ClientSslTransportOptions& setClientEcdsaCert(bool client_ecdsa_cert) {
    client_ecdsa_cert_ = client_ecdsa_cert;
    return *this;
  }

  ClientSslTransportOptions& setCipherSuites(const std::vector<std::string>& cipher_suites) {
    cipher_suites_ = cipher_suites;
    return *this;
  }

  ClientSslTransportOptions& setSigningAlgorithms(const std::vector<std::string>& sigalgs) {
    sigalgs_ = sigalgs;
    return *this;
  }

  ClientSslTransportOptions& setCurves(const std::vector<std::string>& curves) {
    curves_ = curves;
    return *this;
  }

  ClientSslTransportOptions& setSni(absl::string_view sni) {
    sni_ = std::string(sni);
    return *this;
  }

  ClientSslTransportOptions& setTlsVersion(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TlsProtocol tls_version) {
    tls_version_ = tls_version;
    return *this;
  }

  ClientSslTransportOptions& setUseExpiredSpiffeCer(bool use_expired) {
    use_expired_spiffe_cert_ = use_expired;
    return *this;
  }

  ClientSslTransportOptions& setClientWithIntermediateCert(bool intermediate_cert) {
    client_with_intermediate_cert_ = intermediate_cert;
    return *this;
  }

  ClientSslTransportOptions& setCustomCertValidatorConfig(
      envoy::config::core::v3::TypedExtensionConfig* custom_validator_config) {
    custom_validator_config_ = custom_validator_config;
    return *this;
  }

  bool alpn_{};
  bool client_ecdsa_cert_{false};
  std::vector<std::string> cipher_suites_{};
  std::string san_;
  std::vector<std::string> sigalgs_;
  std::vector<std::string> curves_;
  std::string sni_;
  envoy::extensions::transport_sockets::tls::v3::TlsParameters::TlsProtocol tls_version_{
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLS_AUTO};
  bool use_expired_spiffe_cert_{false};
  bool client_with_intermediate_cert_{false};
  // It is owned by the caller that invokes `setCustomCertValidatorConfig()`.
  envoy::config::core::v3::TypedExtensionConfig* custom_validator_config_{nullptr};
};

void initializeUpstreamTlsContextConfig(
    const ClientSslTransportOptions& options,
    envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext& tls_context,
    // By default, clients connect to Envoy. Allow configuring to connect to upstreams.
    bool connect_to_upstream = false);

Network::UpstreamTransportSocketFactoryPtr
createClientSslTransportSocketFactory(const ClientSslTransportOptions& options,
                                      ContextManager& context_manager, Api::Api& api);

Network::DownstreamTransportSocketFactoryPtr
createUpstreamSslContext(ContextManager& context_manager, Api::Api& api, bool use_http3 = false);

Network::DownstreamTransportSocketFactoryPtr
createFakeUpstreamSslContext(const std::string& upstream_cert_name, ContextManager& context_manager,
                             Server::Configuration::TransportSocketFactoryContext& factory_context);

Network::Address::InstanceConstSharedPtr getSslAddress(const Network::Address::IpVersion& version,
                                                       int port);

} // namespace Ssl

namespace Extensions {
namespace TransportSockets {
namespace Tls {

class ContextImplPeer {
public:
  static const Extensions::TransportSockets::Tls::CertValidator&
  getCertValidator(const Extensions::TransportSockets::Tls::ContextImpl& context) {
    return *context.cert_validator_;
  }

  static Extensions::TransportSockets::Tls::CertValidator&
  getMutableCertValidator(const Extensions::TransportSockets::Tls::ContextImpl& context) {
    return *context.cert_validator_;
  }
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions

} // namespace Envoy
