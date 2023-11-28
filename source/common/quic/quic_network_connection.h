#pragma once

#include <memory>

#include "envoy/network/connection.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Quic {

// Read ~32k bytes per connection by default, which is about the same as TCP.
static const uint32_t DEFAULT_PACKETS_TO_READ_PER_CONNECTION = 32u;

class QuicWriteEventCallback {
public:
  virtual ~QuicWriteEventCallback() = default;
  // Called when QUIC finishes a write.
  virtual void onWriteEventDone() PURE;
};

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
  void setEnvoyConnection(Network::Connection& connection, QuicWriteEventCallback& write_callback);

  const Network::ConnectionSocketPtr& connectionSocket() const {
    return connection_sockets_.back();
  }

  // Needed for ENVOY_CONN_LOG.
  uint64_t id() const;

protected:
  Network::Connection::ConnectionStats& connectionStats() const { return *connection_stats_; }

  std::vector<Network::ConnectionSocketPtr>& connectionSockets() { return connection_sockets_; }

  void setConnectionSocket(Network::ConnectionSocketPtr&& connection_socket) {
    connection_sockets_.push_back(std::move(connection_socket));
  }

  void onWriteEventDone();

  Network::Connection* networkConnection() { return envoy_connection_; }

private:
  // TODO(danzh): populate stats.
  std::unique_ptr<Network::Connection::ConnectionStats> connection_stats_;
  // Hosts a list of active sockets, while only the last one is used for writing data.
  // Hosts a single default socket upon construction. New sockets can be pushed in later as a result
  // of QUIC connection migration.
  std::vector<Network::ConnectionSocketPtr> connection_sockets_;
  // Points to an instance of EnvoyQuicServerSession or EnvoyQuicClientSession.
  Network::Connection* envoy_connection_{nullptr};
  QuicWriteEventCallback* write_callback_{nullptr};
};

} // namespace Quic
} // namespace Envoy
