#pragma once

#pragma GCC diagnostic push
// QUICHE allows unused parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
// QUICHE uses offsetof().
#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include "quiche/quic/core/quic_connection.h"

#pragma GCC diagnostic pop

#include <memory>
#include "envoy/network/connection.h"

namespace Envoy {
namespace Quic {

// Override for stats and QoS.
class EnvoyQuicConnection : public quic::QuicConnection {
public:
  EnvoyQuicConnection(quic::QuicConnectionId server_connection_id,
                      quic::QuicSocketAddress initial_peer_address,
                      quic::QuicConnectionHelperInterface* helper,
                      quic::QuicAlarmFactory* alarm_factory, quic::QuicPacketWriter* writer,
                      bool owns_writer, quic::Perspective perspective,
                      const quic::ParsedQuicVersionVector& supported_versions);

  // Called by EnvoyQuicSession::setConnectionStats().
  void setConnectionStats(const Network::Connection::ConnectionStats& stats) {
    connection_stats_ = std::make_unique<Network::Connection::ConnectionStats>(stats);
  }

protected:
  Network::Connection::ConnectionStats& connectionStats() const {
    return *connection_stats_;
  }

private:
   // TODO(danzh): populate stats.
  std::unique_ptr<Network::Connection::ConnectionStats> connection_stats_;
};

} // namespace Quic
} // namespace Envoy
