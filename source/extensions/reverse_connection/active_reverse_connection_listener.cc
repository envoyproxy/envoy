#include "source/extensions/reverse_connection/active_reverse_connection_listener.h"

namespace Envoy {
namespace Extensions {
namespace ReverseConnection {

ActiveReverseConnectionListener::ActiveReverseConnectionListener(
    Network::ConnectionHandler& conn_handler, Event::Dispatcher& dispatcher,
    Network::ListenerConfig& config)
    : Server::OwnedActiveStreamListenerBase(
          conn_handler, dispatcher, std::make_unique<NetworkReverseConnectionListener>(), config) {
  startRCWorkflow(dispatcher, config);
}
ActiveReverseConnectionListener::~ActiveReverseConnectionListener() {
  is_deleting_ = true;
  // Purge sockets that have not progressed to connections. This should only happen when
  // a listener filter stops iteration and never resumes.
  while (!sockets_.empty()) {
    auto removed = sockets_.front()->removeFromList(sockets_);
    dispatcher().deferredDelete(std::move(removed));
  }

  for (auto& [chain, active_connections] : connections_by_context_) {
    ASSERT(active_connections != nullptr);
    auto& connections = active_connections->connections_;
    while (!connections.empty()) {
      connections.front()->connection_->close(Network::ConnectionCloseType::NoFlush,
                                              "reverse_conn_listener_draining");
    }
  }
  dispatcher().clearDeferredDeleteList();
}

ActiveReverseConnectionListener::ActiveReverseConnectionListener(
    Network::ConnectionHandler& conn_handler, Event::Dispatcher& dispatcher,
    Network::ListenerPtr listener, Network::ListenerConfig& config)
    : Server::OwnedActiveStreamListenerBase(conn_handler, dispatcher, std::move(listener), config) {
}

void ActiveReverseConnectionListener::startRCWorkflow(Event::Dispatcher& dispatcher,
                                                      Network::ListenerConfig& config) {
  ENVOY_LOG(debug, "Starting reverse conn workflow on worker: {} listener: {}", dispatcher.name(),
            config.name());
  Network::LocalRevConnRegistry& local_registry =
      dispatcher.connectionHandler()->reverseConnRegistry();
  RELEASE_ASSERT((&local_registry) != nullptr, "Local reverse connection registry.");
  local_registry.getRCManager().registerRCInitiators(config);
}

void ActiveReverseConnectionListener::removeConnection(Server::ActiveTcpConnection& connection) {
  // Remove the connection from all internal data structures maintained by the RCManager.
  const std::string& connectionKey =
      connection.connection_->getSocket()->connectionInfoProvider().localAddress()->asString();
  ENVOY_LOG(
      info,
      "Connection ID :{} local address: {} remote address: {} closed ; Reporting to RCManager",
      connection.connection_->id(), connectionKey,
      connection.connection_->getSocket()
          ->connectionInfoProvider()
          .remoteAddress()
          ->asStringView());

  // Notify that an used reverse connection has been closed.
  dispatcher().connectionHandler()->reverseConnRegistry().getRCManager().notifyConnectionClose(
      connectionKey, true /* is_used */);

  Server::OwnedActiveStreamListenerBase::removeConnection(connection);
}

void ActiveReverseConnectionListener::onAccept(Network::ConnectionSocketPtr&& socket) {
  incNumConnections();
  auto active_socket = std::make_unique<Server::ActiveTcpSocket>(
      *this, std::move(socket), false /* do not hand off at internal listener */);

  onSocketAccepted(std::move(active_socket));
}

void ActiveReverseConnectionListener::onReject(RejectCause cause) {
  switch (cause) {
  case RejectCause::GlobalCxLimit:
    stats_.downstream_global_cx_overflow_.inc();
    break;
  case RejectCause::OverloadAction:
    stats_.downstream_cx_overload_reject_.inc();
    break;
  }
}

void ActiveReverseConnectionListener::recordConnectionsAcceptedOnSocketEvent(
    uint32_t connections_accepted) {
  stats_.connections_accepted_per_socket_event_.recordValue(connections_accepted);
}

void ActiveReverseConnectionListener::updateListenerConfig(Network::ListenerConfig& config) {
  ENVOY_LOG(trace, "replacing listener ", config_->listenerTag(), " by ", config.listenerTag());
  config_ = &config;
}

void ActiveReverseConnectionListener::newActiveConnection(
    const Network::FilterChain& filter_chain, Network::ServerConnectionPtr server_conn_ptr,
    std::unique_ptr<StreamInfo::StreamInfo> stream_info) {
  auto& active_connections = getOrCreateActiveConnections(filter_chain);
  auto active_connection = std::make_unique<Server::ActiveTcpConnection>(
      active_connections, std::move(server_conn_ptr), dispatcher().timeSource(),
      std::move(stream_info));
  // If the connection is already closed, we can just let this connection immediately die.
  if (active_connection->connection_->state() != Network::Connection::State::Closed) {
    ENVOY_CONN_LOG(
        debug, "new connection from {}", *active_connection->connection_,
        active_connection->connection_->connectionInfoProvider().remoteAddress()->asString());
    active_connection->connection_->addConnectionCallbacks(*active_connection);
    LinkedList::moveIntoList(std::move(active_connection), active_connections.connections_);
  }
}

} // namespace ReverseConnection
} // namespace Extensions
} // namespace Envoy
