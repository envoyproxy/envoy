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
                      quic::QuicAlarmFactory& alarm_factory, quic::QuicPacketWriter* writer,
                      bool owns_writer, quic::Perspective perspective,
                      const quic::ParsedQuicVersionVector& supported_versions,
                      Network::ConnectionSocketPtr&& connection_socket);

  ~EnvoyQuicConnection() override;

  // Called by EnvoyQuicSession::setConnectionStats().
  void setConnectionStats(const Network::Connection::ConnectionStats& stats) {
    connection_stats_ = std::make_unique<Network::Connection::ConnectionStats>(stats);
  }

  // Called in session Initialize().
  void setEnvoyConnection(Network::Connection& connection) { envoy_connection_ = &connection; }

  const Network::ConnectionSocketPtr& connectionSocket() const { return connection_socket_; }

  // Needed for ENVOY_CONN_LOG.
  uint64_t id() const;

protected:
  Network::Connection::ConnectionStats& connectionStats() const { return *connection_stats_; }

  Network::Connection& envoyConnection() const {
    ASSERT(envoy_connection_ != nullptr);
    return *envoy_connection_;
  }

  void setConnectionSocket(Network::ConnectionSocketPtr&& connection_socket) {
    connection_socket_ = std::move(connection_socket);
  }

private:
  // TODO(danzh): populate stats.
  std::unique_ptr<Network::Connection::ConnectionStats> connection_stats_;
  // Assigned upon construction. Constructed with empty local address if unknown
  // by then.
  Network::ConnectionSocketPtr connection_socket_;
  // Points to an instance of EnvoyQuicServerSession or EnvoyQuicClientSession.
  Network::Connection* envoy_connection_{nullptr};
};

} // namespace Quic
} // namespace Envoy
