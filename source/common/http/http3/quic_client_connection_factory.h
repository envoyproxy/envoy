#pragma once

#include <string>

#include "envoy/config/listener/v3/quic_config.pb.h"
#include "envoy/config/typed_config.h"
#include "envoy/network/connection.h"
#include "envoy/ssl/context_config.h"

namespace Envoy {
namespace Http {

// A factory to create EnvoyQuicClientConnection instance for QUIC
class QuicClientConnectionFactory : public Config::UntypedFactory {
public:
  ~QuicClientConnectionFactory() override = default;

  virtual std::unique_ptr<Network::ClientConnection>
  createQuicNetworkConnection(Network::Address::InstanceConstSharedPtr server_addr,
                              Network::Address::InstanceConstSharedPtr local_addr,
                              Network::TransportSocketFactory& transport_socket_factory,
                              Stats::Scope& stats_scope, Event::Dispatcher& dispatcher,
                              TimeSource& time_source) PURE;

  std::string category() const override { return "envoy.quic_connection"; }
};

} // namespace Http
} // namespace Envoy
