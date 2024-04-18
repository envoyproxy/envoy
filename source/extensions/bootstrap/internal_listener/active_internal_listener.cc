#include "source/extensions/bootstrap/internal_listener/active_internal_listener.h"

#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"

#include "source/common/listener_manager/active_stream_listener_base.h"
#include "source/common/network/address_impl.h"
#include "source/common/stats/timespan_impl.h"
#include "source/extensions/io_socket/user_space/io_handle.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace InternalListener {

ActiveInternalListener::ActiveInternalListener(Network::ConnectionHandler& conn_handler,
                                               Event::Dispatcher& dispatcher,
                                               Network::ListenerConfig& config)
    : Server::OwnedActiveStreamListenerBase(
          conn_handler, dispatcher,
          std::make_unique<ActiveInternalListener::NetworkInternalListener>(), config) {}

ActiveInternalListener::ActiveInternalListener(Network::ConnectionHandler& conn_handler,
                                               Event::Dispatcher& dispatcher,
                                               Network::ListenerPtr listener,
                                               Network::ListenerConfig& config)
    : Server::OwnedActiveStreamListenerBase(conn_handler, dispatcher, std::move(listener), config) {
}

ActiveInternalListener::~ActiveInternalListener() {
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
                                              "Active Internal Listener destructor");
    }
  }
  dispatcher().clearDeferredDeleteList();
}

void ActiveInternalListener::updateListenerConfig(Network::ListenerConfig& config) {
  ENVOY_LOG(trace, "replacing listener ", config_->listenerTag(), " by ", config.listenerTag());
  config_ = &config;
}

void ActiveInternalListener::onAccept(Network::ConnectionSocketPtr&& socket) {
  // Unlike tcp listener, no rebalancer is applied and won't call pickTargetHandler to account
  // connections.
  incNumConnections();

  auto* io_handle = dynamic_cast<Extensions::IoSocket::UserSpace::IoHandle*>(&socket->ioHandle());
  auto active_socket = std::make_unique<Server::ActiveTcpSocket>(
      *this, std::move(socket), false /* do not hand off at internal listener */);
  // Transfer internal passthrough state to the active socket from downstream.
  if (io_handle != nullptr && io_handle->passthroughState()) {
    io_handle->passthroughState()->mergeInto(active_socket->dynamicMetadata(),
                                             active_socket->filterState());
  }

  onSocketAccepted(std::move(active_socket));
}

void ActiveInternalListener::newActiveConnection(
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
} // namespace InternalListener
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
