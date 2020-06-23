#pragma once

#include "envoy/api/api.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/network/address.h"
#include "envoy/network/transport_socket.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/ssl/context_manager.h"

namespace Envoy {
namespace Ssl {

struct ClientSslTransportOptions {
  ClientSslTransportOptions& setAlpn(bool alpn) {
    alpn_ = alpn;
    return *this;
  }

  ClientSslTransportOptions& setSan(bool san) {
    san_ = san;
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

  ClientSslTransportOptions& setSigningAlgorithmsForTest(const std::string& sigalgs) {
    sigalgs_ = sigalgs;
    return *this;
  }

  ClientSslTransportOptions& setTlsVersion(
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TlsProtocol tls_version) {
    tls_version_ = tls_version;
    return *this;
  }

  ClientSslTransportOptions& setSni(absl::string_view sni) {
    sni_ = std::string(sni);
    return *this;
  }

  bool alpn_{};
  bool san_{};
  bool client_ecdsa_cert_{};
  std::vector<std::string> cipher_suites_{};
  std::string sigalgs_;
  std::string sni_;
  envoy::extensions::transport_sockets::tls::v3::TlsParameters::TlsProtocol tls_version_{
      envoy::extensions::transport_sockets::tls::v3::TlsParameters::TLS_AUTO};
};

Network::TransportSocketFactoryPtr
createClientSslTransportSocketFactory(const ClientSslTransportOptions& options,
                                      ContextManager& context_manager, Api::Api& api);

Network::TransportSocketFactoryPtr createUpstreamSslContext(ContextManager& context_manager,
                                                            Api::Api& api);

Network::TransportSocketFactoryPtr
createFakeUpstreamSslContext(const std::string& upstream_cert_name, ContextManager& context_manager,
                             Server::Configuration::TransportSocketFactoryContext& factory_context);

Network::Address::InstanceConstSharedPtr getSslAddress(const Network::Address::IpVersion& version,
                                                       int port);

} // namespace Ssl
} // namespace Envoy
