#pragma once

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include "quiche/quic/core/quic_connection.h"

#pragma GCC diagnostic pop

#include <memory>

#include "common/common/logger.h"
#include "envoy/network/connection.h"
#include "envoy/network/listener.h"
#include "server/connection_handler_impl.h"

namespace Envoy {
namespace Quic {

// Derived for network filter chain, stats and QoS. This is used on both client
// and server side.
class EnvoyQuicConnection : public quic::QuicConnection,
                            protected Logger::Loggable<Logger::Id::connection> {
public:
  EnvoyQuicConnection(const quic::QuicConnectionId& server_connection_id,
                      quic::QuicSocketAddress initial_peer_address,
                      quic::QuicConnectionHelperInterface& helper,
                      quic::QuicAlarmFactory& alarm_factory, quic::QuicPacketWriter& writer,
                      bool owns_writer, quic::Perspective perspective,
                      const quic::ParsedQuicVersionVector& supported_versions,
                      Network::ListenerConfig& listener_config,
                      Server::ListenerStats& listener_stats);

  ~EnvoyQuicConnection() override = default;

  // Called by EnvoyQuicSession::setConnectionStats().
  void setConnectionStats(const Network::Connection::ConnectionStats& stats) {
    connection_stats_ = std::make_unique<Network::Connection::ConnectionStats>(stats);
  }

  // quic::QuicConnection
  // Overridden to retrieve filter chain with initialized self address.
  bool OnPacketHeader(const quic::QuicPacketHeader& header) override;

  // Called in session Initialize().
  void setEnvoyConnection(Network::Connection& connection) { envoy_connection_ = &connection; }

  const Network::ConnectionSocketPtr& connectionSocket() const { return connection_socket_; }

protected:
  Network::Connection::ConnectionStats& connectionStats() const { return *connection_stats_; }

private:
  // TODO(danzh): populate stats.
  std::unique_ptr<Network::Connection::ConnectionStats> connection_stats_;
  // Only initialized after self address is known. Must not own the underlying
  // socket because UDP socket is shared among all connections.
  Network::ConnectionSocketPtr connection_socket_;
  Network::Connection* envoy_connection_{nullptr};
  Network::ListenerConfig& listener_config_;
  Server::ListenerStats& listener_stats_;
  // Latched to the corresponding quic FilterChain after connection_socket_ is
  // initialized.
  const Network::FilterChain* filter_chain_{nullptr};
};

} // namespace Quic
} // namespace Envoy
