#pragma once

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

  ClientSslTransportOptions& setCipherSuites(const std::vector<std::string>& cipher_suites) {
    cipher_suites_ = cipher_suites;
    return *this;
  }

  ClientSslTransportOptions&
  setTlsVersion(envoy::api::v2::auth::TlsParameters_TlsProtocol tls_version) {
    tls_version_ = tls_version;
    return *this;
  }

  ClientSslTransportOptions& setSigningAlgorithmsForTest(const std::string& sigalgs) {
    sigalgs_ = sigalgs;
    return *this;
  }

  bool alpn_{};
  bool san_{};
  std::vector<std::string> cipher_suites_{};
  envoy::api::v2::auth::TlsParameters_TlsProtocol tls_version_{
      envoy::api::v2::auth::TlsParameters::TLS_AUTO};
  std::string sigalgs_;
};

Network::TransportSocketFactoryPtr
createClientSslTransportSocketFactory(const ClientSslTransportOptions& options,
                                      ContextManager& context_manager);
Network::TransportSocketFactoryPtr createUpstreamSslContext(ContextManager& context_manager);

Network::Address::InstanceConstSharedPtr getSslAddress(const Network::Address::IpVersion& version,
                                                       int port);

} // namespace Ssl
} // namespace Envoy
