#include "server/active_stream_listener.h"

#include "envoy/common/time.h"

#include "common/stats/timespan_impl.h"

#include "extensions/transport_sockets/well_known_names.h"

namespace Envoy {
namespace Server {

namespace {
void emitLogs(Network::ListenerConfig& config, StreamInfo::StreamInfo& stream_info) {
  stream_info.onRequestComplete();
  for (const auto& access_log : config.accessLogs()) {
    access_log->log(nullptr, nullptr, nullptr, stream_info);
  }
}
} // namespace

void ActiveTcpListener::removeConnection(ActiveTcpConnection& connection) {
  ENVOY_CONN_LOG(debug, "adding to cleanup list", *connection.connection_);
  ActiveConnections& active_connections = connection.active_connections_;
  ActiveTcpConnectionPtr removed = connection.removeFromList(active_connections.connections_);
  parent_.dispatcher().deferredDelete(std::move(removed));
  // Delete map entry only iff connections becomes empty.
  if (active_connections.connections_.empty()) {
    auto iter = connections_by_context_.find(&active_connections.filter_chain_);
    ASSERT(iter != connections_by_context_.end());
    // To cover the lifetime of every single connection, Connections need to be deferred deleted
    // because the previously contained connection is deferred deleted.
    parent_.dispatcher().deferredDelete(std::move(iter->second));
    // The erase will break the iteration over the connections_by_context_ during the deletion.
    if (!is_deleting_) {
      connections_by_context_.erase(iter);
    }
  }
}

ActiveTcpListener::ActiveTcpListener(Network::TcpConnectionHandler& parent,
                                     Network::ListenerConfig& config)
    : ActiveTcpListener(
          parent,
          parent.dispatcher().createListener(config.listenSocketFactory().getListenSocket(), *this,
                                             config.bindToPort(), config.tcpBacklogSize()),
          config) {}

ActiveTcpListener::ActiveTcpListener(Network::TcpConnectionHandler& parent,
                                     Network::ListenerPtr&& listener,
                                     Network::ListenerConfig& config)
    : ActiveListenerImplBase(parent, &config), parent_(parent), listener_(std::move(listener)),
      listener_filters_timeout_(config.listenerFiltersTimeout()),
      continue_on_listener_filters_timeout_(config.continueOnListenerFiltersTimeout()) {
  config.connectionBalancer().registerHandler(*this);
}

void ActiveTcpListener::updateListenerConfig(Network::ListenerConfig& config) {
  ENVOY_LOG(trace, "replacing listener ", config_->listenerTag(), " by ", config.listenerTag());
  ASSERT(&config_->connectionBalancer() == &config.connectionBalancer());
  config_ = &config;
}

ActiveTcpListener::~ActiveTcpListener() {
  is_deleting_ = true;
  config_->connectionBalancer().unregisterHandler(*this);

  // Purge sockets that have not progressed to connections. This should only happen when
  // a listener filter stops iteration and never resumes.
  while (!sockets_.empty()) {
    ActiveTcpSocketPtr removed = sockets_.front()->removeFromList(sockets_);
    parent_.dispatcher().deferredDelete(std::move(removed));
  }

  for (auto& chain_and_connections : connections_by_context_) {
    ASSERT(chain_and_connections.second != nullptr);
    auto& connections = chain_and_connections.second->connections_;
    while (!connections.empty()) {
      connections.front()->connection_->close(Network::ConnectionCloseType::NoFlush);
    }
  }
  parent_.dispatcher().clearDeferredDeleteList();

  // By the time a listener is destroyed, in the common case, there should be no connections.
  // However, this is not always true if there is an in flight rebalanced connection that is
  // being posted. This assert is extremely useful for debugging the common path so we will leave it
  // for now. If it becomes a problem (developers hitting this assert when using debug builds) we
  // can revisit. This case, if it happens, should be benign on production builds. This case is
  // covered in ConnectionHandlerTest::RemoveListenerDuringRebalance.
  ASSERT(num_listener_connections_ == 0);
}

void ActiveTcpSocket::onTimeout() {
  listener_.stats_.downstream_pre_cx_timeout_.inc();
  ASSERT(inserted());
  ENVOY_LOG(debug, "listener filter times out after {} ms",
            listener_.listener_filters_timeout_.count());

  if (listener_.continue_on_listener_filters_timeout_) {
    ENVOY_LOG(debug, "fallback to default listener filter");
    newConnection();
  }
  unlink();
}

void ActiveTcpSocket::startTimer() {
  if (listener_.listener_filters_timeout_.count() > 0) {
    timer_ = listener_.parent_.dispatcher().createTimer([this]() -> void { onTimeout(); });
    timer_->enableTimer(listener_.listener_filters_timeout_);
  }
}

void ActiveTcpSocket::unlink() {
  ActiveTcpSocketPtr removed = removeFromList(listener_.sockets_);
  if (removed->timer_ != nullptr) {
    removed->timer_->disableTimer();
  }
  // Emit logs if a connection is not established.
  if (!connected_) {
    emitLogs(*listener_.config_, *stream_info_);
  }
  listener_.parent_.dispatcher().deferredDelete(std::move(removed));
}

void ActiveTcpSocket::continueFilterChain(bool success) {
  if (success) {
    bool no_error = true;
    if (iter_ == accept_filters_.end()) {
      iter_ = accept_filters_.begin();
    } else {
      iter_ = std::next(iter_);
    }

    for (; iter_ != accept_filters_.end(); iter_++) {
      Network::FilterStatus status = (*iter_)->onAccept(*this);
      if (status == Network::FilterStatus::StopIteration) {
        // The filter is responsible for calling us again at a later time to continue the filter
        // chain from the next filter.
        if (!socket().ioHandle().isOpen()) {
          // break the loop but should not create new connection
          no_error = false;
          break;
        } else {
          // Blocking at the filter but no error
          return;
        }
      }
    }
    // Successfully ran all the accept filters.
    if (no_error) {
      newConnection();
    } else {
      // Signal the caller that no extra filter chain iteration is needed.
      iter_ = accept_filters_.end();
    }
  }

  // Filter execution concluded, unlink and delete this ActiveTcpSocket if it was linked.
  if (inserted()) {
    unlink();
  }
}

void ActiveTcpSocket::setDynamicMetadata(const std::string& name,
                                         const ProtobufWkt::Struct& value) {
  stream_info_->setDynamicMetadata(name, value);
}

void ActiveTcpSocket::newConnection() {
  connected_ = true;

  // Check if the socket may need to be redirected to another listener.
  Network::BalancedConnectionHandlerOptRef new_listener;

  if (hand_off_restored_destination_connections_ &&
      socket_->addressProvider().localAddressRestored()) {
    // Find a listener associated with the original destination address.
    new_listener =
        listener_.parent_.getBalancedHandlerByAddress(*socket_->addressProvider().localAddress());
  }
  if (new_listener.has_value()) {
    // Hands off connections redirected by iptables to the listener associated with the
    // original destination address. Pass 'hand_off_restored_destination_connections' as false to
    // prevent further redirection as well as 'rebalanced' as true since the connection has
    // already been balanced if applicable inside onAcceptWorker() when the connection was
    // initially accepted. Note also that we must account for the number of connections properly
    // across both listeners.
    // TODO(mattklein123): See note in ~ActiveTcpSocket() related to making this accounting better.
    listener_.decNumConnections();
    new_listener.value().get().incNumConnections();
    new_listener.value().get().onAcceptWorker(std::move(socket_), false, true);
  } else {
    // Set default transport protocol if none of the listener filters did it.
    if (socket_->detectedTransportProtocol().empty()) {
      socket_->setDetectedTransportProtocol(
          Extensions::TransportSockets::TransportProtocolNames::get().RawBuffer);
    }
    // TODO(lambdai): add integration test
    // TODO: Address issues in wider scope. See https://github.com/envoyproxy/envoy/issues/8925
    // Erase accept filter states because accept filters may not get the opportunity to clean up.
    // Particularly the assigned events need to reset before assigning new events in the follow up.
    accept_filters_.clear();
    // Create a new connection on this listener.
    listener_.newConnection(std::move(socket_), std::move(stream_info_));
  }
}

void ActiveTcpListener::onAccept(Network::ConnectionSocketPtr&& socket) {
  if (listenerConnectionLimitReached()) {
    ENVOY_LOG(trace, "closing connection: listener connection limit reached for {}",
              config_->name());
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

  // Create and run the filters
  config_->filterChainFactory().createListenerFilterChain(*active_socket);
  active_socket->continueFilterChain(true);

  // Move active_socket to the sockets_ list if filter iteration needs to continue later.
  // Otherwise we let active_socket be destructed when it goes out of scope.
  if (active_socket->iter_ != active_socket->accept_filters_.end()) {
    active_socket->startTimer();
    LinkedList::moveIntoListBack(std::move(active_socket), sockets_);
  } else {
    // If active_socket is about to be destructed, emit logs if a connection is not created.
    if (!active_socket->connected_) {
      emitLogs(*config_, *active_socket->stream_info_);
    }
  }
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

void ActiveTcpListener::newConnection(Network::ConnectionSocketPtr&& socket,
                                      std::unique_ptr<StreamInfo::StreamInfo> stream_info) {

  // Find matching filter chain.
  const auto filter_chain = config_->filterChainManager().findFilterChain(*socket);
  if (filter_chain == nullptr) {
    ENVOY_LOG(debug, "closing connection: no matching filter chain found");
    stats_.no_filter_chain_match_.inc();
    stream_info->setResponseFlag(StreamInfo::ResponseFlag::NoRouteFound);
    stream_info->setResponseCodeDetails(StreamInfo::ResponseCodeDetails::get().FilterChainNotFound);
    emitLogs(*config_, *stream_info);
    socket->close();
    return;
  }

  stream_info->setFilterChainName(filter_chain->name());
  auto transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  stream_info->setDownstreamSslConnection(transport_socket->ssl());
  auto& active_connections = getOrCreateActiveConnections(*filter_chain);
  auto server_conn_ptr = parent_.dispatcher().createServerConnection(
      std::move(socket), std::move(transport_socket), *stream_info);
  if (const auto timeout = filter_chain->transportSocketConnectTimeout();
      timeout != std::chrono::milliseconds::zero()) {
    server_conn_ptr->setTransportSocketConnectTimeout(timeout);
  }
  ActiveTcpConnectionPtr active_connection(
      new ActiveTcpConnection(active_connections, std::move(server_conn_ptr),
                              parent_.dispatcher().timeSource(), std::move(stream_info)));
  active_connection->connection_->setBufferLimits(config_->perConnectionBufferLimitBytes());

  const bool empty_filter_chain = !config_->filterChainFactory().createNetworkFilterChain(
      *active_connection->connection_, filter_chain->networkFilterFactories());
  if (empty_filter_chain) {
    ENVOY_CONN_LOG(debug, "closing connection: no filters", *active_connection->connection_);
    active_connection->connection_->close(Network::ConnectionCloseType::NoFlush);
  }

  // If the connection is already closed, we can just let this connection immediately die.
  if (active_connection->connection_->state() != Network::Connection::State::Closed) {
    ENVOY_CONN_LOG(debug, "new connection", *active_connection->connection_);
    active_connection->connection_->addConnectionCallbacks(*active_connection);
    LinkedList::moveIntoList(std::move(active_connection), active_connections.connections_);
  }
}

ActiveConnections&
ActiveTcpListener::getOrCreateActiveConnections(const Network::FilterChain& filter_chain) {
  ActiveConnectionsPtr& connections = connections_by_context_[&filter_chain];
  if (connections == nullptr) {
    connections = std::make_unique<ActiveConnections>(*this, filter_chain);
  }
  return *connections;
}

void ActiveTcpListener::deferredRemoveFilterChains(
    const std::list<const Network::FilterChain*>& draining_filter_chains) {
  // Need to recover the original deleting state.
  const bool was_deleting = is_deleting_;
  is_deleting_ = true;
  for (const auto* filter_chain : draining_filter_chains) {
    auto iter = connections_by_context_.find(filter_chain);
    if (iter == connections_by_context_.end()) {
      // It is possible when listener is stopping.
    } else {
      auto& connections = iter->second->connections_;
      while (!connections.empty()) {
        connections.front()->connection_->close(Network::ConnectionCloseType::NoFlush);
      }
      // Since is_deleting_ is on, we need to manually remove the map value and drive the iterator.
      // Defer delete connection container to avoid race condition in destroying connection.
      parent_.dispatcher().deferredDelete(std::move(iter->second));
      connections_by_context_.erase(iter);
    }
  }
  is_deleting_ = was_deleting;
}

namespace {
// Structure used to allow a unique_ptr to be captured in a posted lambda. See below.
struct RebalancedSocket {
  Network::ConnectionSocketPtr socket;
};
using RebalancedSocketSharedPtr = std::shared_ptr<RebalancedSocket>;
} // namespace

void ActiveTcpListener::post(Network::ConnectionSocketPtr&& socket) {
  // It is not possible to capture a unique_ptr because the post() API copies the lambda, so we must
  // bundle the socket inside a shared_ptr that can be captured.
  // TODO(mattklein123): It may be possible to change the post() API such that the lambda is only
  // moved, but this is non-trivial and needs investigation.
  RebalancedSocketSharedPtr socket_to_rebalance = std::make_shared<RebalancedSocket>();
  socket_to_rebalance->socket = std::move(socket);

  parent_.dispatcher().post([socket_to_rebalance, tag = config_->listenerTag(), &parent = parent_,
                             handoff = config_->handOffRestoredDestinationConnections()]() {
    auto balanced_handler = parent.getBalancedHandlerByTag(tag);
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
  stream_info_->setConnectionID(connection_->id());

  // Active connections on the handler (not listener). The per listener connections have already
  // been incremented at this point either via the connection balancer or in the socket accept
  // path if there is no configured balancer.
  listener.parent_.incNumConnections();
}

ActiveTcpConnection::~ActiveTcpConnection() {
  emitLogs(*active_connections_.listener_.config_, *stream_info_);
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
} // namespace Server
} // namespace Envoy