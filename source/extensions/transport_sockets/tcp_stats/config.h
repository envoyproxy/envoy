#pragma once

#include "envoy/extensions/transport_sockets/tcp_stats/v3/tcp_stats.pb.h"
#include "envoy/server/transport_socket_config.h"

#include "source/extensions/transport_sockets/common/passthrough.h"
#include "source/extensions/transport_sockets/tcp_stats/tcp_stats.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace TcpStats {

class TcpStatsSocketFactory {
public:
  TcpStatsSocketFactory(Server::Configuration::TransportSocketFactoryContext& context,
                        const envoy::extensions::transport_sockets::tcp_stats::v3::Config& config);

protected:
#if defined(__linux__)
  ConfigConstSharedPtr config_;
#endif
};

class UpstreamTcpStatsSocketFactory : public TcpStatsSocketFactory, public PassthroughFactory {
public:
  UpstreamTcpStatsSocketFactory(
      Server::Configuration::TransportSocketFactoryContext& context,
      const envoy::extensions::transport_sockets::tcp_stats::v3::Config& config,
      Network::UpstreamTransportSocketFactoryPtr&& inner_factory);

  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsConstSharedPtr options,
                        Upstream::HostDescriptionConstSharedPtr host) const override;
};

class DownstreamTcpStatsSocketFactory : public TcpStatsSocketFactory,
                                        public DownstreamPassthroughFactory {
public:
  DownstreamTcpStatsSocketFactory(
      Server::Configuration::TransportSocketFactoryContext& context,
      const envoy::extensions::transport_sockets::tcp_stats::v3::Config& config,
      Network::DownstreamTransportSocketFactoryPtr&& inner_factory);

  Network::TransportSocketPtr createDownstreamTransportSocket() const override;
};

} // namespace TcpStats
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
