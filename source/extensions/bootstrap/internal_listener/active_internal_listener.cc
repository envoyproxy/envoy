#include "source/extensions/bootstrap/internal_listener/active_internal_listener.h"

#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"

#include "source/common/listener_manager/active_stream_listener_base.h"
#include "source/common/network/address_impl.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/stats/timespan_impl.h"
#include "source/extensions/io_socket/user_space/io_handle.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace InternalListener {

namespace {

// Returns the state shared with the client side of the connection, if the connection's socket is a
// user space socket and propagating close details is enabled. Otherwise returns nullptr.
IoSocket::UserSpace::PassthroughStateSharedPtr
closeDetailsPassthroughState(const Network::Connection& connection) {
  if (!Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.internal_listener_propagate_close_details")) {
    return nullptr;
  }
  auto* io_handle =
      dynamic_cast<IoSocket::UserSpace::IoHandle*>(&connection.getSocket()->ioHandle());
  return io_handle != nullptr ? io_handle->passthroughState() : nullptr;
}

} // namespace

ActiveInternalConnection::ActiveInternalConnection(
    Server::ActiveConnections& active_connections, Network::ConnectionPtr&& new_connection,
    TimeSource& time_system, std::unique_ptr<StreamInfo::StreamInfo>&& stream_info,
    IoSocket::UserSpace::PassthroughStateSharedPtr passthrough_state)
    : Server::ActiveTcpConnection(active_connections, std::move(new_connection), time_system,
                                  std::move(stream_info)),
      passthrough_state_(std::move(passthrough_state)) {}

void ActiveInternalConnection::recordCloseDetails() {
  if (passthrough_state_ == nullptr) {
    return;
  }
  const StreamInfo::StreamInfo& stream_info = connection_->streamInfo();
  absl::string_view upstream_transport_failure_reason;
  if (stream_info.upstreamInfo().has_value()) {
    upstream_transport_failure_reason =
        stream_info.upstreamInfo()->upstreamTransportFailureReason();
  }
  passthrough_state_->setCloseDetails(connection_->localCloseReason(),
                                      upstream_transport_failure_reason);
}

void ActiveInternalConnection::onEvent(Network::ConnectionEvent event) {
  // Record why this side closed the connection. A close initiated by the client side is not
  // recorded, as the client side already knows why it closed.
  if (event == Network::ConnectionEvent::LocalClose) {
    recordCloseDetails();
  }
  Server::ActiveTcpConnection::onEvent(event);
}

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
      *this, std::move(socket), false /* do not hand off at internal listener */, std::nullopt);
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
  IoSocket::UserSpace::PassthroughStateSharedPtr passthrough_state =
      closeDetailsPassthroughState(*server_conn_ptr);
  auto active_connection = std::make_unique<ActiveInternalConnection>(
      active_connections, std::move(server_conn_ptr), dispatcher().timeSource(),
      std::move(stream_info), std::move(passthrough_state));
  // If the connection is already closed, we can just let this connection immediately die.
  if (active_connection->connection_->state() != Network::Connection::State::Closed) {
    ENVOY_CONN_LOG(
        debug, "new connection from {}", *active_connection->connection_,
        active_connection->connection_->connectionInfoProvider().remoteAddress()->asString());
    active_connection->connection_->addConnectionCallbacks(*active_connection);
    LinkedList::moveIntoList(Server::ActiveConnectionPtr{std::move(active_connection)},
                             active_connections.connections_);
  } else {
    // This side closed the connection while setting it up, e.g. a network filter closed it when it
    // was created, before the connection callbacks were added.
    active_connection->recordCloseDetails();
  }
}
} // namespace InternalListener
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
