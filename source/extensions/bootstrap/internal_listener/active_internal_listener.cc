#include "source/extensions/bootstrap/internal_listener/active_internal_listener.h"

#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"

#include "source/common/listener_manager/active_stream_listener_base.h"
#include "source/common/network/address_impl.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/stats/timespan_impl.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace InternalListener {

namespace {

constexpr absl::string_view CloseDetailsLocalClosePrefix = "internal_listener_close:";
constexpr absl::string_view CloseDetailsUpstreamFailurePrefix =
    "internal_listener_upstream_failure:";

// Formats the local close reason and upstream transport failure reason of an internal listener
// connection, omitting empty fields.
std::string formatCloseDetails(absl::string_view local_close_reason,
                               absl::string_view upstream_transport_failure_reason) {
  if (local_close_reason.empty() && upstream_transport_failure_reason.empty()) {
    return {};
  }
  if (upstream_transport_failure_reason.empty()) {
    return absl::StrCat(CloseDetailsLocalClosePrefix, local_close_reason);
  }
  if (local_close_reason.empty()) {
    return absl::StrCat(CloseDetailsUpstreamFailurePrefix, upstream_transport_failure_reason);
  }
  return absl::StrCat(CloseDetailsLocalClosePrefix, local_close_reason, "|",
                      CloseDetailsUpstreamFailurePrefix, upstream_transport_failure_reason);
}

// Returns the state shared with the client side of the connection if the connection's socket is a
// user space socket. Otherwise returns nullptr.
IoSocket::UserSpace::PassthroughStateSharedPtr
closeDetailsPassthroughState(const Network::Connection& connection) {
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

void ActiveInternalConnection::onEvent(Network::ConnectionEvent event) {
  // Record why this side closed the connection. A close initiated by the client side is not
  // recorded, as the client side already knows why it closed.
  if (event == Network::ConnectionEvent::LocalClose && passthrough_state_ != nullptr &&
      Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.internal_listener_propagate_close_details")) {
    const StreamInfo::StreamInfo& stream_info = connection_->streamInfo();
    absl::string_view upstream_transport_failure_reason;
    if (stream_info.upstreamInfo().has_value()) {
      upstream_transport_failure_reason =
          stream_info.upstreamInfo()->upstreamTransportFailureReason();
    }
    const std::string details =
        formatCloseDetails(connection_->localCloseReason(), upstream_transport_failure_reason);
    if (!details.empty()) {
      passthrough_state_->setCloseDetails(details);
    }
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

  auto active_socket = std::make_unique<Server::ActiveTcpSocket>(
      *this, std::move(socket), false /* do not hand off at internal listener */, std::nullopt);
  onSocketAccepted(std::move(active_socket));
}

void ActiveInternalListener::newActiveConnection(
    const Network::FilterChain& filter_chain, Network::ServerConnectionPtr server_conn_ptr,
    std::unique_ptr<StreamInfo::StreamInfo> stream_info) {
  auto& active_connections = getOrCreateActiveConnections(filter_chain);
  IoSocket::UserSpace::PassthroughStateSharedPtr passthrough_state =
      server_conn_ptr->state() != Network::Connection::State::Closed
          ? closeDetailsPassthroughState(*server_conn_ptr)
          : nullptr;
  Server::ActiveConnectionPtr active_connection = std::make_unique<ActiveInternalConnection>(
      active_connections, std::move(server_conn_ptr), dispatcher().timeSource(),
      std::move(stream_info), std::move(passthrough_state));
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
