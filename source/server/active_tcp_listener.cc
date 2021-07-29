#include "source/server/active_tcp_listener.h"

#include <chrono>

#include "envoy/event/dispatcher.h"
#include "envoy/stats/scope.h"

#include "source/common/common/assert.h"
#include "source/common/network/connection_impl.h"
#include "source/common/network/utility.h"
#include "source/common/stats/timespan_impl.h"

namespace Envoy {
namespace Server {

ActiveTcpListener::ActiveTcpListener(Network::TcpConnectionHandler& parent,
                                     Network::ListenerConfig& config, uint32_t worker_index)
    : ActiveStreamListenerBase(parent, parent.dispatcher(),
                               parent.dispatcher().createListener(
                                   config.listenSocketFactory().getListenSocket(worker_index),
                                   *this, config.bindToPort()),
                               config),
      tcp_conn_handler_(parent) {
  config.connectionBalancer().registerHandler(*this);
}

ActiveTcpListener::ActiveTcpListener(Network::TcpConnectionHandler& parent,
                                     Network::ListenerPtr&& listener,
                                     Network::ListenerConfig& config)
    : ActiveStreamListenerBase(parent, parent.dispatcher(), std::move(listener), config),
      tcp_conn_handler_(parent) {
  config.connectionBalancer().registerHandler(*this);
}

ActiveTcpListener::~ActiveTcpListener() {
  is_deleting_ = true;
  config_->connectionBalancer().unregisterHandler(*this);

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
      connections.front()->connection_->close(Network::ConnectionCloseType::NoFlush);
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

void ActiveTcpListener::removeConnection(ActiveTcpConnection& connection) {
  ENVOY_CONN_LOG(debug, "adding to cleanup list", *connection.connection_);
  ActiveConnections& active_connections = connection.active_connections_;
  auto removed = connection.removeFromList(active_connections.connections_);
  dispatcher().deferredDelete(std::move(removed));
  // Delete map entry only iff connections becomes empty.
  if (active_connections.connections_.empty()) {
    auto iter = connections_by_context_.find(&active_connections.filter_chain_);
    ASSERT(iter != connections_by_context_.end());
    // To cover the lifetime of every single connection, Connections need to be deferred deleted
    // because the previously contained connection is deferred deleted.
    dispatcher().deferredDelete(std::move(iter->second));
    // The erase will break the iteration over the connections_by_context_ during the deletion.
    if (!is_deleting_) {
      connections_by_context_.erase(iter);
    }
  }
}

void ActiveTcpListener::updateListenerConfig(Network::ListenerConfig& config) {
  ENVOY_LOG(trace, "replacing listener ", config_->listenerTag(), " by ", config.listenerTag());
  ASSERT(&config_->connectionBalancer() == &config.connectionBalancer());
  config_ = &config;
}

void ActiveTcpListener::removeFilterChain(const Network::FilterChain* filter_chain) {
  auto iter = connections_by_context_.find(filter_chain);
  if (iter == connections_by_context_.end()) {
    // It is possible when listener is stopping.
  } else {
    auto& connections = iter->second->connections_;
    while (!connections.empty()) {
      connections.front()->connection_->close(Network::ConnectionCloseType::NoFlush);
    }
    // Since is_deleting_ is on, we need to manually remove the map value and drive the
    // iterator. Defer delete connection container to avoid race condition in destroying
    // connection.
    dispatcher().deferredDelete(std::move(iter->second));
    connections_by_context_.erase(iter);
  }
}

void ActiveTcpListener::onAccept(Network::ConnectionSocketPtr&& socket) {
  if (listenerConnectionLimitReached()) {
    RELEASE_ASSERT(socket->addressProvider().remoteAddress() != nullptr, "");
    ENVOY_LOG(trace, "closing connection from {}: listener connection limit reached for {}",
              socket->addressProvider().remoteAddress()->asString(), config_->name());
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

void ActiveTcpListener::onAcceptWorker(Network::ConnectionSocketPtr&& socket,
                                       bool hand_off_restored_destination_connections,
                                       bool rebalanced) {
  if (!rebalanced) {
    Network::BalancedConnectionHandler& target_handler =
        config_->connectionBalancer().pickTargetHandler(*this);
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
  ActiveTcpConnectionPtr active_connection(
      new ActiveTcpConnection(active_connections, std::move(server_conn_ptr),
                              dispatcher().timeSource(), std::move(stream_info)));
  // If the connection is already closed, we can just let this connection immediately die.
  if (active_connection->connection_->state() != Network::Connection::State::Closed) {
    ENVOY_CONN_LOG(debug, "new connection from {}", *active_connection->connection_,
                   active_connection->connection_->addressProvider().remoteAddress()->asString());
    active_connection->connection_->addConnectionCallbacks(*active_connection);
    LinkedList::moveIntoList(std::move(active_connection), active_connections.connections_);
  }
}

ActiveConnections&
ActiveTcpListener::getOrCreateActiveConnections(const Network::FilterChain& filter_chain) {
  ActiveConnectionCollectionPtr& connections = connections_by_context_[&filter_chain];
  if (connections == nullptr) {
    connections = std::make_unique<ActiveConnections>(*this, filter_chain);
  }
  return *connections;
}

void ActiveTcpListener::post(Network::ConnectionSocketPtr&& socket) {
  // It is not possible to capture a unique_ptr because the post() API copies the lambda, so we must
  // bundle the socket inside a shared_ptr that can be captured.
  // TODO(mattklein123): It may be possible to change the post() API such that the lambda is only
  // moved, but this is non-trivial and needs investigation.
  RebalancedSocketSharedPtr socket_to_rebalance = std::make_shared<RebalancedSocket>();
  socket_to_rebalance->socket = std::move(socket);

  dispatcher().post([socket_to_rebalance, tag = config_->listenerTag(),
                     &tcp_conn_handler = tcp_conn_handler_,
                     handoff = config_->handOffRestoredDestinationConnections()]() {
    auto balanced_handler = tcp_conn_handler.getBalancedHandlerByTag(tag);
    if (balanced_handler.has_value()) {
      balanced_handler->get().onAcceptWorker(std::move(socket_to_rebalance->socket), handoff, true);
      return;
    }
  });
}

ActiveConnections::ActiveConnections(ActiveTcpListener& listener,
                                     const Network::FilterChain& filter_chain)
    : listener_(listener), filter_chain_(filter_chain) {}

ActiveConnections::~ActiveConnections() {
  // connections should be defer deleted already.
  ASSERT(connections_.empty());
}

ActiveTcpConnection::ActiveTcpConnection(ActiveConnections& active_connections,
                                         Network::ConnectionPtr&& new_connection,
                                         TimeSource& time_source,
                                         std::unique_ptr<StreamInfo::StreamInfo>&& stream_info)
    : stream_info_(std::move(stream_info)), active_connections_(active_connections),
      connection_(std::move(new_connection)),
      conn_length_(new Stats::HistogramCompletableTimespanImpl(
          active_connections_.listener_.stats_.downstream_cx_length_ms_, time_source)) {
  // We just universally set no delay on connections. Theoretically we might at some point want
  // to make this configurable.
  connection_->noDelay(true);
  auto& listener = active_connections_.listener_;
  listener.stats_.downstream_cx_total_.inc();
  listener.stats_.downstream_cx_active_.inc();
  listener.per_worker_stats_.downstream_cx_total_.inc();
  listener.per_worker_stats_.downstream_cx_active_.inc();

  // Active connections on the handler (not listener). The per listener connections have already
  // been incremented at this point either via the connection balancer or in the socket accept
  // path if there is no configured balancer.
  listener.parent_.incNumConnections();
}

ActiveTcpConnection::~ActiveTcpConnection() {
  ActiveStreamListenerBase::emitLogs(*active_connections_.listener_.config_, *stream_info_);
  auto& listener = active_connections_.listener_;
  listener.stats_.downstream_cx_active_.dec();
  listener.stats_.downstream_cx_destroy_.inc();
  listener.per_worker_stats_.downstream_cx_active_.dec();
  conn_length_->complete();

  // Active listener connections (not handler).
  listener.decNumConnections();

  // Active handler connections (not listener).
  listener.parent_.decNumConnections();
}

void ActiveTcpConnection::onEvent(Network::ConnectionEvent event) {
  ENVOY_LOG(trace, "[C{}] connection on event {}", connection_->id(), static_cast<int>(event));
  // Any event leads to destruction of the connection.
  if (event == Network::ConnectionEvent::LocalClose ||
      event == Network::ConnectionEvent::RemoteClose) {
    active_connections_.listener_.removeConnection(*this);
  }
}

} // namespace Server
} // namespace Envoy
