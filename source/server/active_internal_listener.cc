#include "server/active_internal_listener.h"

#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"

#include "common/stats/timespan_impl.h"

#include "server/connection_handler_impl.h"

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

void ActiveInternalSocket::onTimeout() {
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

void ActiveInternalSocket::startTimer() {
  if (listener_.listener_filters_timeout_.count() > 0) {
    timer_ = listener_.dispatcher().createTimer([this]() -> void { onTimeout(); });
    timer_->enableTimer(listener_.listener_filters_timeout_);
  }
}

void ActiveInternalSocket::unlink() {
  auto removed = removeFromList(listener_.sockets_);
  if (removed->timer_ != nullptr) {
    removed->timer_->disableTimer();
  }
  // Emit logs if a connection is not established.
  if (!connected_) {
    emitLogs(*listener_.config_, *stream_info_);
  }
  listener_.dispatcher().deferredDelete(std::move(removed));
}

void ActiveInternalSocket::continueFilterChain(bool success) {
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

void ActiveInternalSocket::setDynamicMetadata(const std::string& name,
                                              const ProtobufWkt::Struct& value) {
  stream_info_->setDynamicMetadata(name, value);
}

void ActiveInternalSocket::newConnection() {
  connected_ = true;

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

void ActiveInternalListener::removeConnection(ActiveInternalConnection& connection) {
  ENVOY_CONN_LOG(debug, "adding to cleanup list", *connection.connection_);
  ActiveInternalConnections& active_connections = connection.active_connections_;
  ActiveInternalConnectionPtr removed = connection.removeFromList(active_connections.connections_);
  dispatcher().deferredDelete(std::move(removed));
  // Delete map entry only iff connections becomes empty.
  if (active_connections.connections_.empty()) {
    auto iter = connections_by_context_.find(&active_connections.filter_chain_);
    ASSERT(iter != connections_by_context_.end());
    // To cover the lifetime of every single connection, Connections need to be deferred deleted
    // because the previously contained connection is deferred deleted.
    dispatcher().deferredDelete(std::move(iter->second));
    // The erase will break the iteration over the connections_by_context_ during the deletion.
    // if (!is_deleting_) {
    //   connections_by_context_.erase(`iter`);
    // }
  }
}

void ActiveInternalListener::newConnection(Network::ConnectionSocketPtr&& socket,
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
  auto server_conn_ptr = dispatcher().createServerConnection(
      std::move(socket), std::move(transport_socket), *stream_info);
  if (const auto timeout = filter_chain->transportSocketConnectTimeout();
      timeout != std::chrono::milliseconds::zero()) {
    server_conn_ptr->setTransportSocketConnectTimeout(timeout);
  }
  ActiveInternalConnectionPtr active_connection(
      new ActiveInternalConnection(active_connections, std::move(server_conn_ptr),
                                   dispatcher().timeSource(), std::move(stream_info)));
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

ActiveInternalConnections&
ActiveInternalListener::getOrCreateActiveConnections(const Network::FilterChain& filter_chain) {
  ActiveInternalConnectionsPtr& connections = connections_by_context_[&filter_chain];
  if (connections == nullptr) {
    connections = std::make_unique<ActiveInternalConnections>(*this, filter_chain);
  }
  return *connections;
}

void ActiveInternalListener::deferredRemoveFilterChains(
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
      dispatcher().deferredDelete(std::move(iter->second));
      connections_by_context_.erase(iter);
    }
  }
  is_deleting_ = was_deleting;
}

void ActiveInternalListener::updateListenerConfig(Network::ListenerConfig& config) {
  ENVOY_LOG(trace, "replacing listener ", config_->listenerTag(), " by ", config.listenerTag());
  config_ = &config;
}

ActiveInternalConnections::ActiveInternalConnections(ActiveInternalListener& listener,
                                                     const Network::FilterChain& filter_chain)
    : listener_(listener), filter_chain_(filter_chain) {}

ActiveInternalConnections::~ActiveInternalConnections() {
  // connections should be defer deleted already.
  ASSERT(connections_.empty());
}

ActiveInternalConnection::ActiveInternalConnection(
    ActiveInternalConnections& active_connections, Network::ConnectionPtr&& new_connection,
    TimeSource& time_source, std::unique_ptr<StreamInfo::StreamInfo>&& stream_info)
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
  // TODO(lambdai): FIX-ME
  //++listener.parent_.num_handler_connections_;
}

ActiveInternalConnection::~ActiveInternalConnection() {
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

ActiveInternalListener::ActiveInternalListener(Network::ConnectionHandler& parent,
                                               Network::ListenerPtr&& listener,
                                               Network::ListenerConfig& config)
    : ConnectionHandlerImpl::ActiveListenerImplBase(parent, &config), parent_(parent),
      listener_(std::move(listener)), listener_filters_timeout_(config.listenerFiltersTimeout()),
      continue_on_listener_filters_timeout_(config.continueOnListenerFiltersTimeout()) {}

ActiveInternalListener::~ActiveInternalListener() {
  // TODO(lambdai): delete the active connections.
  // ASSERT(num_listener_connections_ == 0);
}

void ActiveInternalListener::onAccept(Network::ConnectionSocketPtr&& socket) {

  auto active_socket = std::make_unique<ActiveInternalSocket>(
      *this, std::move(socket), false /* do not handle off at internal listener */);

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
} // namespace Server
} // namespace Envoy