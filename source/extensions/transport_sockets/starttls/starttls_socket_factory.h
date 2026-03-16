#pragma once

#include "envoy/network/transport_socket.h"
#include "envoy/ssl/context.h"
#include "envoy/upstream/host_description.h"

#include "source/common/common/logger.h"
#include "source/common/network/transport_socket_options_impl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace StartTls {

class StartTlsSocketFactory : public Network::CommonUpstreamTransportSocketFactory,
                              Logger::Loggable<Logger::Id::config> {
public:
  ~StartTlsSocketFactory() override = default;

  StartTlsSocketFactory(Network::UpstreamTransportSocketFactoryPtr raw_socket_factory,
                        Network::UpstreamTransportSocketFactoryPtr tls_socket_factory)
      : raw_socket_factory_(std::move(raw_socket_factory)),
        tls_socket_factory_(std::move(tls_socket_factory)) {}

  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsConstSharedPtr options,
                        Upstream::HostDescriptionConstSharedPtr host) const override;
  bool implementsSecureTransport() const override { return false; }
  absl::string_view defaultServerNameIndication() const override { return ""; }
  Envoy::Ssl::ClientContextSharedPtr sslCtx() override { return tls_socket_factory_->sslCtx(); }
  OptRef<const Ssl::ClientContextConfig> clientContextConfig() const override {
    return tls_socket_factory_->clientContextConfig();
  }

private:
  Network::UpstreamTransportSocketFactoryPtr raw_socket_factory_;
  Network::UpstreamTransportSocketFactoryPtr tls_socket_factory_;
};

class StartTlsDownstreamSocketFactory : public Network::DownstreamTransportSocketFactory,
                                        Logger::Loggable<Logger::Id::config> {
public:
  ~StartTlsDownstreamSocketFactory() override = default;

  StartTlsDownstreamSocketFactory(Network::DownstreamTransportSocketFactoryPtr raw_socket_factory,
                                  Network::DownstreamTransportSocketFactoryPtr tls_socket_factory)
      : raw_socket_factory_(std::move(raw_socket_factory)),
        tls_socket_factory_(std::move(tls_socket_factory)) {}

  Network::TransportSocketPtr createDownstreamTransportSocket() const override;
  bool implementsSecureTransport() const override { return false; }

private:
  Network::DownstreamTransportSocketFactoryPtr raw_socket_factory_;
  Network::DownstreamTransportSocketFactoryPtr tls_socket_factory_;
};

} // namespace StartTls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
