#pragma once

#include <string>

#include "envoy/config/core/v3/protocol.pb.h"
#include "envoy/config/typed_config.h"
#include "envoy/network/connection.h"
#include "envoy/ssl/context_config.h"

namespace Envoy {
namespace Http {

// Store quic helpers which can be shared between connections and must live
// beyond the lifetime of individual connections.
struct PersistentQuicInfo {
  virtual ~PersistentQuicInfo() = default;
};

// A factory to create EnvoyQuicClientSession and EnvoyQuicClientConnection for QUIC
class QuicClientConnectionFactory : public Config::UntypedFactory {
public:
  ~QuicClientConnectionFactory() override = default;

  // Allows thread-local creation of PersistentQuicInfo.
  // Each (thread local) connection pool can call createNetworkConnectionInfo to create a
  // PersistentQuicInfo, then use that PersistentQuicInfo. when calling createQuicNetworkConnection
  // for all the connections in that pool.
  virtual std::unique_ptr<PersistentQuicInfo>
  createNetworkConnectionInfo(Event::Dispatcher& dispatcher,
                              Network::TransportSocketFactory& transport_socket_factory,
                              Stats::Scope& stats_scope, TimeSource& time_source,
                              Network::Address::InstanceConstSharedPtr server_addr) PURE;

  virtual std::unique_ptr<Network::ClientConnection>
  createQuicNetworkConnection(PersistentQuicInfo& info, Event::Dispatcher& dispatcher,
                              Network::Address::InstanceConstSharedPtr server_addr,
                              Network::Address::InstanceConstSharedPtr local_addr) PURE;

  std::string category() const override { return "envoy.quic_connection"; }
};

} // namespace Http
} // namespace Envoy
