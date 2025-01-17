#include "source/common/listener_manager/active_tcp_listener.h"

#include <chrono>

#include "envoy/event/dispatcher.h"
#include "envoy/stats/scope.h"

#include "source/common/common/assert.h"
#include "source/common/network/connection_impl.h"
#include "source/common/network/utility.h"

namespace Envoy {
namespace Server {

ActiveTcpListener::ActiveTcpListener(Network::TcpConnectionHandler& parent,
                                     Network::ListenerConfig& config, Runtime::Loader& runtime,
                                     Random::RandomGenerator& random,
                                     Network::SocketSharedPtr&& socket,
                                     Network::Address::InstanceConstSharedPtr& listen_address,
                                     Network::ConnectionBalancer& connection_balancer,
                                     ThreadLocalOverloadStateOptRef overload_state)
    : OwnedActiveStreamListenerBase(
          parent, parent.dispatcher(),
          parent.createListener(std::move(socket), *this, runtime, random, config, overload_state),
          config),
      tcp_conn_handler_(parent), connection_balancer_(connection_balancer),
      listen_address_(listen_address) {
  connection_balancer_.registerHandler(*this);
}

ActiveTcpListener::ActiveTcpListener(Network::TcpConnectionHandler& parent,
                                     Network::ListenerPtr&& listener,
                                     Network::Address::InstanceConstSharedPtr& listen_address,
                                     Network::ListenerConfig& config,
                                     Network::ConnectionBalancer& connection_balancer,
                                     Runtime::Loader&)
    : OwnedActiveStreamListenerBase(parent, parent.dispatcher(), std::move(listener), config),
      tcp_conn_handler_(parent), connection_balancer_(connection_balancer),
      listen_address_(listen_address) {
  connection_balancer_.registerHandler(*this);
}

ActiveTcpListener::~ActiveTcpListener() {
  is_deleting_ = true;
  connection_balancer_.unregisterHandler(*this);

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
      connections.front()->connection_->close(
          Network::ConnectionCloseType::NoFlush,
          "purging_socket_that_have_not_progressed_to_connections");
    }
  }
  dispatcher().clearDeferredDeleteList();

  // By the time a listener is destroyed, in the common case, there should be no connections.
  // However, this is not always true if there is an in flight rebalanced connection that is
  // being posted. This assert is extremely useful for debugging the common path so we will leave it
  // for now. If it becomes a problem (developers hitting this assert when using debug builds) we
  // can revisit. This case, if it happens, should be benign on production builds. This case is
  // covered in ConnectionHandlerTest::RemoveListenerDuringRebalance.
  ASSERT(num_listener_connections_ == 0, fmt::format("destroyed listener {} has {} connections",
                                                     config_->name(), numConnections()));
}

void ActiveTcpListener::updateListenerConfig(Network::ListenerConfig& config) {
  ENVOY_LOG(trace, "replacing listener ", config_->listenerTag(), " by ", config.listenerTag());
  config_ = &config;
}

void ActiveTcpListener::onAccept(Network::ConnectionSocketPtr&& socket) {
  if (listenerConnectionLimitReached()) {
    RELEASE_ASSERT(socket->connectionInfoProvider().remoteAddress() != nullptr, "");
    ENVOY_LOG(trace, "closing connection from {}: listener connection limit reached for {}",
              socket->connectionInfoProvider().remoteAddress()->asString(), config_->name());
    socket->close();
    stats_.downstream_cx_overflow_.inc();
    return;
  }

  onAcceptWorker(std::move(socket), config_->handOffRestoredDestinationConnections(), false);
}

void ActiveTcpListener::onReject(RejectCause cause) {
  switch (cause) {
  case RejectCause::GlobalCxLimit:
    stats_.downstream_global_cx_overflow_.inc();
    break;
  case RejectCause::OverloadAction:
    stats_.downstream_cx_overload_reject_.inc();
    break;
  }
}

void ActiveTcpListener::recordConnectionsAcceptedOnSocketEvent(uint32_t connections_accepted) {
  stats_.connections_accepted_per_socket_event_.recordValue(connections_accepted);
}

void ActiveTcpListener::onAcceptWorker(Network::ConnectionSocketPtr&& socket,
                                       bool hand_off_restored_destination_connections,
                                       bool rebalanced) {
  // Get Round Trip Time
  absl::optional<std::chrono::milliseconds> t = socket->lastRoundTripTime();
  if (t.has_value()) {
    socket->connectionInfoProvider().setRoundTripTime(t.value());
  }

  if (!rebalanced) {
    Network::BalancedConnectionHandler& target_handler =
        connection_balancer_.pickTargetHandler(*this);
    if (&target_handler != this) {
      target_handler.post(std::move(socket));
      return;
    }
  }

  auto active_socket = std::make_unique<ActiveTcpSocket>(*this, std::move(socket),
                                                         hand_off_restored_destination_connections);

  onSocketAccepted(std::move(active_socket));
}

void ActiveTcpListener::pauseListening() {
  if (listener_ != nullptr) {
    listener_->disable();
  }
}

void ActiveTcpListener::resumeListening() {
  if (listener_ != nullptr) {
    listener_->enable();
  }
}

Network::BalancedConnectionHandlerOptRef
ActiveTcpListener::getBalancedHandlerByAddress(const Network::Address::Instance& address) {
  return tcp_conn_handler_.getBalancedHandlerByAddress(address);
}

void ActiveTcpListener::newActiveConnection(const Network::FilterChain& filter_chain,
                                            Network::ServerConnectionPtr server_conn_ptr,
                                            std::unique_ptr<StreamInfo::StreamInfo> stream_info) {
  auto& active_connections = getOrCreateActiveConnections(filter_chain);
  auto active_connection =
      std::make_unique<ActiveTcpConnection>(active_connections, std::move(server_conn_ptr),
                                            dispatcher().timeSource(), std::move(stream_info));
  // If the connection is already closed, we can just let this connection immediately die.
  if (active_connection->connection_->state() != Network::Connection::State::Closed) {
    ENVOY_CONN_LOG(
        debug, "new connection from {}", *active_connection->connection_,
        active_connection->connection_->connectionInfoProvider().remoteAddress()->asString());
    active_connection->connection_->addConnectionCallbacks(*active_connection);
    LinkedList::moveIntoList(std::move(active_connection), active_connections.connections_);
  }
}

void ActiveTcpListener::post(Network::ConnectionSocketPtr&& socket) {
  // It is not possible to capture a unique_ptr because the post() API copies the lambda, so we must
  // bundle the socket inside a shared_ptr that can be captured.
  // TODO(mattklein123): It may be possible to change the post() API such that the lambda is only
  // moved, but this is non-trivial and needs investigation.
  RebalancedSocketSharedPtr socket_to_rebalance = std::make_shared<RebalancedSocket>();
  socket_to_rebalance->socket = std::move(socket);

  dispatcher().post([socket_to_rebalance, address = listen_address_, tag = config_->listenerTag(),
                     &tcp_conn_handler = tcp_conn_handler_,
                     handoff = config_->handOffRestoredDestinationConnections()]() {
    auto balanced_handler = tcp_conn_handler.getBalancedHandlerByTag(tag, *address);
    if (balanced_handler.has_value()) {
      balanced_handler->get().onAcceptWorker(std::move(socket_to_rebalance->socket), handoff, true);
      return;
    }
  });
}

} // namespace Server
} // namespace Envoy
