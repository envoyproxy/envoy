#pragma once

#include "envoy/extensions/transport_sockets/tcp_stats/v3/tcp_stats.pb.h"
#include "envoy/server/transport_socket_config.h"

#include "source/extensions/transport_sockets/common/passthrough.h"
#include "source/extensions/transport_sockets/tcp_stats/tcp_stats.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace TcpStats {

class TcpStatsSocketFactory : public PassthroughFactory {
public:
  TcpStatsSocketFactory(Server::Configuration::TransportSocketFactoryContext& context,
                        const envoy::extensions::transport_sockets::tcp_stats::v3::Config& config,
                        Network::TransportSocketFactoryPtr&& inner_factory);

  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsConstSharedPtr options) const override;

private:
#if defined(__linux__)
  ConfigConstSharedPtr config_;
#endif
};

} // namespace TcpStats
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
