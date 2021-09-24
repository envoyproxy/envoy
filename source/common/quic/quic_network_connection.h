#pragma once

#include <memory>

#include "envoy/network/connection.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Quic {

// Read ~32k bytes per connection by default, which is about the same as TCP.
static const uint32_t DEFAULT_PACKETS_TO_READ_PER_CONNECTION = 32u;

// A base class of both the client and server connections which keeps stats and
// connection socket.
class QuicNetworkConnection : protected Logger::Loggable<Logger::Id::connection> {
public:
  QuicNetworkConnection(Network::ConnectionSocketPtr&& connection_socket);

  virtual ~QuicNetworkConnection();

  // Called by EnvoyQuicSession::setConnectionStats().
  void setConnectionStats(const Network::Connection::ConnectionStats& stats) {
    connection_stats_ = std::make_unique<Network::Connection::ConnectionStats>(stats);
  }

  // Called in session Initialize().
  void setEnvoyConnection(Network::Connection& connection) { envoy_connection_ = &connection; }

  const Network::ConnectionSocketPtr& connectionSocket() const { 
    //std::cout << "socket list size " << connection_sockets_.size() << std::endl;
    return connection_sockets_.back(); }

  // Needed for ENVOY_CONN_LOG.
  uint64_t id() const;

protected:
  Network::Connection::ConnectionStats& connectionStats() const { return *connection_stats_; }

  std::vector<Network::ConnectionSocketPtr>& connectionSockets() { return connection_sockets_; }

  void setConnectionSocket(Network::ConnectionSocketPtr&& connection_socket) {
    connection_sockets_.push_back(std::move(connection_socket));
  }

private:
  // TODO(danzh): populate stats.
  std::unique_ptr<Network::Connection::ConnectionStats> connection_stats_;
  // Assigned upon construction. Constructed with empty local address if unknown
  // by then. The last one is the default active socket.
  std::vector<Network::ConnectionSocketPtr> connection_sockets_;
  // Points to an instance of EnvoyQuicServerSession or EnvoyQuicClientSession.
  Network::Connection* envoy_connection_{nullptr};
};

} // namespace Quic
} // namespace Envoy
