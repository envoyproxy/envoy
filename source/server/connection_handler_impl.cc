#include "server/connection_handler_impl.h"

#include <chrono>

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/network/exception.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/timespan.h"

#include "common/event/deferred_task.h"
#include "common/network/connection_impl.h"
#include "common/network/utility.h"
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

ConnectionHandlerImpl::ConnectionHandlerImpl(Event::Dispatcher& dispatcher,
                                             absl::optional<uint32_t> worker_index)
    : worker_index_(worker_index), dispatcher_(dispatcher),
      per_handler_stat_prefix_(dispatcher.name() + "."), disable_listeners_(false) {}

void ConnectionHandlerImpl::incNumConnections() { ++num_handler_connections_; }

void ConnectionHandlerImpl::decNumConnections() {
  ASSERT(num_handler_connections_ > 0);
  --num_handler_connections_;
}

void ConnectionHandlerImpl::addListener(absl::optional<uint64_t> overridden_listener,
                                        Network::ListenerConfig& config) {
  ActiveListenerDetails details;
  if (config.listenSocketFactory().socketType() == Network::Socket::Type::Stream) {
    if (overridden_listener.has_value()) {
      for (auto& listener : listeners_) {
        if (listener.second.listener_->listenerTag() == overridden_listener) {
          listener.second.tcpListener()->get().updateListenerConfig(config);
          return;
        }
      }
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
    auto tcp_listener = std::make_unique<ActiveTcpListener>(*this, config);
    details.typed_listener_ = *tcp_listener;
    details.listener_ = std::move(tcp_listener);
  } else {
    ASSERT(config.udpListenerFactory() != nullptr, "UDP listener factory is not initialized.");
    ASSERT(worker_index_.has_value());
    ConnectionHandler::ActiveUdpListenerPtr udp_listener =
        config.udpListenerFactory()->createActiveUdpListener(*worker_index_, *this, dispatcher_,
                                                             config);
    details.typed_listener_ = *udp_listener;
    details.listener_ = std::move(udp_listener);
  }
  if (disable_listeners_) {
    details.listener_->pauseListening();
  }
  if (auto* listener = details.listener_->listener(); listener != nullptr) {
    listener->setRejectFraction(listener_reject_fraction_);
  }
  listeners_.emplace_back(config.listenSocketFactory().localAddress(), std::move(details));
}

void ConnectionHandlerImpl::removeListeners(uint64_t listener_tag) {
  for (auto listener = listeners_.begin(); listener != listeners_.end();) {
    if (listener->second.listener_->listenerTag() == listener_tag) {
      listener = listeners_.erase(listener);
    } else {
      ++listener;
    }
  }
}

ConnectionHandlerImpl::ActiveListenerDetailsOptRef
ConnectionHandlerImpl::findActiveListenerByTag(uint64_t listener_tag) {
  // TODO(mattklein123): We should probably use a hash table here to lookup the tag
  // instead of iterating through the listener list.
  for (auto& listener : listeners_) {
    if (listener.second.listener_->listener() != nullptr &&
        listener.second.listener_->listenerTag() == listener_tag) {
      return listener.second;
    }
  }

  return absl::nullopt;
}

Network::UdpListenerCallbacksOptRef
ConnectionHandlerImpl::getUdpListenerCallbacks(uint64_t listener_tag) {
  auto listener = findActiveListenerByTag(listener_tag);
  if (listener.has_value()) {
    // If the tag matches this must be a UDP listener.
    auto udp_listener = listener->get().udpListener();
    ASSERT(udp_listener.has_value());
    return udp_listener;
  }

  return absl::nullopt;
}

void ConnectionHandlerImpl::removeFilterChains(
    uint64_t listener_tag, const std::list<const Network::FilterChain*>& filter_chains,
    std::function<void()> completion) {
  for (auto& listener : listeners_) {
    if (listener.second.listener_->listenerTag() == listener_tag) {
      listener.second.tcpListener()->get().deferredRemoveFilterChains(filter_chains);
      // Completion is deferred because the above removeFilterChains() may defer delete connection.
      Event::DeferredTaskUtil::deferredRun(dispatcher_, std::move(completion));
      return;
    }
  }
  NOT_REACHED_GCOVR_EXCL_LINE;
}

void ConnectionHandlerImpl::stopListeners(uint64_t listener_tag) {
  for (auto& listener : listeners_) {
    if (listener.second.listener_->listenerTag() == listener_tag) {
      listener.second.listener_->shutdownListener();
    }
  }
}

void ConnectionHandlerImpl::stopListeners() {
  for (auto& listener : listeners_) {
    listener.second.listener_->shutdownListener();
  }
}

void ConnectionHandlerImpl::disableListeners() {
  disable_listeners_ = true;
  for (auto& listener : listeners_) {
    listener.second.listener_->pauseListening();
  }
}

void ConnectionHandlerImpl::enableListeners() {
  disable_listeners_ = false;
  for (auto& listener : listeners_) {
    listener.second.listener_->resumeListening();
  }
}

void ConnectionHandlerImpl::setListenerRejectFraction(float reject_fraction) {
  listener_reject_fraction_ = reject_fraction;
  for (auto& listener : listeners_) {
    listener.second.listener_->listener()->setRejectFraction(reject_fraction);
  }
}

void ConnectionHandlerImpl::ActiveTcpListener::removeConnection(ActiveTcpConnection& connection) {
  ENVOY_CONN_LOG(debug, "adding to cleanup list", *connection.connection_);
  ActiveConnections& active_connections = connection.active_connections_;
  ActiveTcpConnectionPtr removed = connection.removeFromList(active_connections.connections_);
  parent_.dispatcher_.deferredDelete(std::move(removed));
  // Delete map entry only iff connections becomes empty.
  if (active_connections.connections_.empty()) {
    auto iter = connections_by_context_.find(&active_connections.filter_chain_);
    ASSERT(iter != connections_by_context_.end());
    // To cover the lifetime of every single connection, Connections need to be deferred deleted
    // because the previously contained connection is deferred deleted.
    parent_.dispatcher_.deferredDelete(std::move(iter->second));
    // The erase will break the iteration over the connections_by_context_ during the deletion.
    if (!is_deleting_) {
      connections_by_context_.erase(iter);
    }
  }
}

ConnectionHandlerImpl::ActiveListenerImplBase::ActiveListenerImplBase(
    Network::ConnectionHandler& parent, Network::ListenerConfig* config)
    : stats_({ALL_LISTENER_STATS(POOL_COUNTER(config->listenerScope()),
                                 POOL_GAUGE(config->listenerScope()),
                                 POOL_HISTOGRAM(config->listenerScope()))}),
      per_worker_stats_({ALL_PER_HANDLER_LISTENER_STATS(
          POOL_COUNTER_PREFIX(config->listenerScope(), parent.statPrefix()),
          POOL_GAUGE_PREFIX(config->listenerScope(), parent.statPrefix()))}),
      config_(config) {}

ConnectionHandlerImpl::ActiveTcpListener::ActiveTcpListener(ConnectionHandlerImpl& parent,
                                                            Network::ListenerConfig& config)
    : ActiveTcpListener(
          parent,
          parent.dispatcher_.createListener(config.listenSocketFactory().getListenSocket(), *this,
                                            config.bindToPort(), config.tcpBacklogSize()),
          config) {}

ConnectionHandlerImpl::ActiveTcpListener::ActiveTcpListener(ConnectionHandlerImpl& parent,
                                                            Network::ListenerPtr&& listener,
                                                            Network::ListenerConfig& config)
    : ConnectionHandlerImpl::ActiveListenerImplBase(parent, &config), parent_(parent),
      listener_(std::move(listener)), listener_filters_timeout_(config.listenerFiltersTimeout()),
      continue_on_listener_filters_timeout_(config.continueOnListenerFiltersTimeout()) {
  config.connectionBalancer().registerHandler(*this);
}

void ConnectionHandlerImpl::ActiveTcpListener::updateListenerConfig(
    Network::ListenerConfig& config) {
  ENVOY_LOG(trace, "replacing listener ", config_->listenerTag(), " by ", config.listenerTag());
  ASSERT(&config_->connectionBalancer() == &config.connectionBalancer());
  config_ = &config;
}

ConnectionHandlerImpl::ActiveTcpListener::~ActiveTcpListener() {
  is_deleting_ = true;
  config_->connectionBalancer().unregisterHandler(*this);

  // Purge sockets that have not progressed to connections. This should only happen when
  // a listener filter stops iteration and never resumes.
  while (!sockets_.empty()) {
    ActiveTcpSocketPtr removed = sockets_.front()->removeFromList(sockets_);
    parent_.dispatcher_.deferredDelete(std::move(removed));
  }

  for (auto& chain_and_connections : connections_by_context_) {
    ASSERT(chain_and_connections.second != nullptr);
    auto& connections = chain_and_connections.second->connections_;
    while (!connections.empty()) {
      connections.front()->connection_->close(Network::ConnectionCloseType::NoFlush);
    }
  }
  parent_.dispatcher_.clearDeferredDeleteList();

  // By the time a listener is destroyed, in the common case, there should be no connections.
  // However, this is not always true if there is an in flight rebalanced connection that is
  // being posted. This assert is extremely useful for debugging the common path so we will leave it
  // for now. If it becomes a problem (developers hitting this assert when using debug builds) we
  // can revisit. This case, if it happens, should be benign on production builds. This case is
  // covered in ConnectionHandlerTest::RemoveListenerDuringRebalance.
  ASSERT(num_listener_connections_ == 0);
}

ConnectionHandlerImpl::ActiveTcpListenerOptRef
ConnectionHandlerImpl::findActiveTcpListenerByAddress(const Network::Address::Instance& address) {
  // This is a linear operation, may need to add a map<address, listener> to improve performance.
  // However, linear performance might be adequate since the number of listeners is small.
  // We do not return stopped listeners.
  auto listener_it = std::find_if(
      listeners_.begin(), listeners_.end(),
      [&address](std::pair<Network::Address::InstanceConstSharedPtr, ActiveListenerDetails>& p) {
        return p.second.tcpListener().has_value() && p.second.listener_->listener() != nullptr &&
               p.first->type() == Network::Address::Type::Ip && *(p.first) == address;
      });

  // If there is exact address match, return the corresponding listener.
  if (listener_it != listeners_.end()) {
    return listener_it->second.tcpListener();
  }

  // Otherwise, we need to look for the wild card match, i.e., 0.0.0.0:[address_port].
  // We do not return stopped listeners.
  // TODO(wattli): consolidate with previous search for more efficiency.
  listener_it = std::find_if(
      listeners_.begin(), listeners_.end(),
      [&address](
          const std::pair<Network::Address::InstanceConstSharedPtr, ActiveListenerDetails>& p) {
        return absl::holds_alternative<std::reference_wrapper<ActiveTcpListener>>(
                   p.second.typed_listener_) &&
               p.second.listener_->listener() != nullptr &&
               p.first->type() == Network::Address::Type::Ip &&
               p.first->ip()->port() == address.ip()->port() && p.first->ip()->isAnyAddress();
      });
  return (listener_it != listeners_.end())
             ? ActiveTcpListenerOptRef(absl::get<std::reference_wrapper<ActiveTcpListener>>(
                   listener_it->second.typed_listener_))
             : absl::nullopt;
}

void ConnectionHandlerImpl::ActiveTcpSocket::onTimeout() {
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

void ConnectionHandlerImpl::ActiveTcpSocket::startTimer() {
  if (listener_.listener_filters_timeout_.count() > 0) {
    timer_ = listener_.parent_.dispatcher_.createTimer([this]() -> void { onTimeout(); });
    timer_->enableTimer(listener_.listener_filters_timeout_);
  }
}

void ConnectionHandlerImpl::ActiveTcpSocket::unlink() {
  ActiveTcpSocketPtr removed = removeFromList(listener_.sockets_);
  if (removed->timer_ != nullptr) {
    removed->timer_->disableTimer();
  }
  // Emit logs if a connection is not established.
  if (!connected_) {
    emitLogs(*listener_.config_, *stream_info_);
  }
  listener_.parent_.dispatcher_.deferredDelete(std::move(removed));
}

void ConnectionHandlerImpl::ActiveTcpSocket::continueFilterChain(bool success) {
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

void ConnectionHandlerImpl::ActiveTcpSocket::setDynamicMetadata(const std::string& name,
                                                                const ProtobufWkt::Struct& value) {
  stream_info_->setDynamicMetadata(name, value);
}

void ConnectionHandlerImpl::ActiveTcpSocket::newConnection() {
  connected_ = true;

  // Check if the socket may need to be redirected to another listener.
  ActiveTcpListenerOptRef new_listener;

  if (hand_off_restored_destination_connections_ && socket_->localAddressRestored()) {
    // Find a listener associated with the original destination address.
    new_listener = listener_.parent_.findActiveTcpListenerByAddress(*socket_->localAddress());
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

void ConnectionHandlerImpl::ActiveTcpListener::onAccept(Network::ConnectionSocketPtr&& socket) {
  if (listenerConnectionLimitReached()) {
    ENVOY_LOG(trace, "closing connection: listener connection limit reached for {}",
              config_->name());
    socket->close();
    stats_.downstream_cx_overflow_.inc();
    return;
  }

  onAcceptWorker(std::move(socket), config_->handOffRestoredDestinationConnections(), false);
}

void ConnectionHandlerImpl::ActiveTcpListener::onReject(RejectCause cause) {
  switch (cause) {
  case RejectCause::GlobalCxLimit:
    stats_.downstream_global_cx_overflow_.inc();
    break;
  case RejectCause::OverloadAction:
    stats_.downstream_cx_overload_reject_.inc();
    break;
  }
}

void ConnectionHandlerImpl::ActiveTcpListener::onAcceptWorker(
    Network::ConnectionSocketPtr&& socket, bool hand_off_restored_destination_connections,
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

void ConnectionHandlerImpl::ActiveTcpListener::pauseListening() {
  if (listener_ != nullptr) {
    listener_->disable();
  }
}

void ConnectionHandlerImpl::ActiveTcpListener::resumeListening() {
  if (listener_ != nullptr) {
    listener_->enable();
  }
}

void ConnectionHandlerImpl::ActiveTcpListener::newConnection(
    Network::ConnectionSocketPtr&& socket, std::unique_ptr<StreamInfo::StreamInfo> stream_info) {
  // Refresh local address in case it was restored by a listener filter like the original_dst
  // filter.
  stream_info->setDownstreamLocalAddress(socket->localAddress());

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

  auto transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  stream_info->setDownstreamSslConnection(transport_socket->ssl());
  auto& active_connections = getOrCreateActiveConnections(*filter_chain);
  auto server_conn_ptr = parent_.dispatcher_.createServerConnection(
      std::move(socket), std::move(transport_socket), *stream_info);
  if (const auto timeout = filter_chain->transportSocketConnectTimeout();
      timeout != std::chrono::milliseconds::zero()) {
    server_conn_ptr->setTransportSocketConnectTimeout(timeout);
  }
  ActiveTcpConnectionPtr active_connection(
      new ActiveTcpConnection(active_connections, std::move(server_conn_ptr),
                              parent_.dispatcher_.timeSource(), std::move(stream_info)));
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

ConnectionHandlerImpl::ActiveConnections&
ConnectionHandlerImpl::ActiveTcpListener::getOrCreateActiveConnections(
    const Network::FilterChain& filter_chain) {
  ActiveConnectionsPtr& connections = connections_by_context_[&filter_chain];
  if (connections == nullptr) {
    connections = std::make_unique<ConnectionHandlerImpl::ActiveConnections>(*this, filter_chain);
  }
  return *connections;
}

void ConnectionHandlerImpl::ActiveTcpListener::deferredRemoveFilterChains(
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
      parent_.dispatcher_.deferredDelete(std::move(iter->second));
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

void ConnectionHandlerImpl::ActiveTcpListener::post(Network::ConnectionSocketPtr&& socket) {
  // It is not possible to capture a unique_ptr because the post() API copies the lambda, so we must
  // bundle the socket inside a shared_ptr that can be captured.
  // TODO(mattklein123): It may be possible to change the post() API such that the lambda is only
  // moved, but this is non-trivial and needs investigation.
  RebalancedSocketSharedPtr socket_to_rebalance = std::make_shared<RebalancedSocket>();
  socket_to_rebalance->socket = std::move(socket);

  parent_.dispatcher_.post(
      [socket_to_rebalance, tag = config_->listenerTag(), &parent = parent_]() {
        auto listener = parent.findActiveListenerByTag(tag);
        if (listener.has_value()) {
          // If the tag matches this must be a TCP listener.
          ASSERT(absl::holds_alternative<std::reference_wrapper<ActiveTcpListener>>(
              listener->get().typed_listener_));
          auto& tcp_listener =
              absl::get<std::reference_wrapper<ActiveTcpListener>>(listener->get().typed_listener_)
                  .get();
          tcp_listener.onAcceptWorker(std::move(socket_to_rebalance->socket),
                                      tcp_listener.config_->handOffRestoredDestinationConnections(),
                                      true);
          return;
        }
      });
}

ConnectionHandlerImpl::ActiveConnections::ActiveConnections(
    ConnectionHandlerImpl::ActiveTcpListener& listener, const Network::FilterChain& filter_chain)
    : listener_(listener), filter_chain_(filter_chain) {}

ConnectionHandlerImpl::ActiveConnections::~ActiveConnections() {
  // connections should be defer deleted already.
  ASSERT(connections_.empty());
}

ConnectionHandlerImpl::ActiveTcpConnection::ActiveTcpConnection(
    ActiveConnections& active_connections, Network::ConnectionPtr&& new_connection,
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
  ++listener.parent_.num_handler_connections_;
}

ConnectionHandlerImpl::ActiveTcpConnection::~ActiveTcpConnection() {
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

ConnectionHandlerImpl::ActiveTcpListenerOptRef
ConnectionHandlerImpl::ActiveListenerDetails::tcpListener() {
  auto* val = absl::get_if<std::reference_wrapper<ActiveTcpListener>>(&typed_listener_);
  return (val != nullptr) ? absl::make_optional(*val) : absl::nullopt;
}

ConnectionHandlerImpl::UdpListenerCallbacksOptRef
ConnectionHandlerImpl::ActiveListenerDetails::udpListener() {
  auto* val = absl::get_if<std::reference_wrapper<Network::UdpListenerCallbacks>>(&typed_listener_);
  return (val != nullptr) ? absl::make_optional(*val) : absl::nullopt;
}

ActiveUdpListenerBase::ActiveUdpListenerBase(uint32_t worker_index, uint32_t concurrency,
                                             Network::ConnectionHandler& parent,
                                             Network::Socket& listen_socket,
                                             Network::UdpListenerPtr&& listener,
                                             Network::ListenerConfig* config)
    : ConnectionHandlerImpl::ActiveListenerImplBase(parent, config), worker_index_(worker_index),
      concurrency_(concurrency), parent_(parent), listen_socket_(listen_socket),
      udp_listener_(std::move(listener)) {
  ASSERT(worker_index_ < concurrency_);
  config_->udpListenerWorkerRouter()->get().registerWorkerForListener(*this);
}

ActiveUdpListenerBase::~ActiveUdpListenerBase() {
  config_->udpListenerWorkerRouter()->get().unregisterWorkerForListener(*this);
}

void ActiveUdpListenerBase::post(Network::UdpRecvData&& data) {
  ASSERT(!udp_listener_->dispatcher().isThreadSafe(),
         "Shouldn't be post'ing if thread safe; use onWorkerData() instead.");

  // It is not possible to capture a unique_ptr because the post() API copies the lambda, so we must
  // bundle the socket inside a shared_ptr that can be captured.
  // TODO(mattklein123): It may be possible to change the post() API such that the lambda is only
  // moved, but this is non-trivial and needs investigation.
  auto data_to_post = std::make_shared<Network::UdpRecvData>();
  *data_to_post = std::move(data);

  udp_listener_->dispatcher().post(
      [data_to_post, tag = config_->listenerTag(), &parent = parent_]() {
        Network::UdpListenerCallbacksOptRef listener = parent.getUdpListenerCallbacks(tag);
        if (listener.has_value()) {
          listener->get().onDataWorker(std::move(*data_to_post));
        }
      });
}

void ActiveUdpListenerBase::onData(Network::UdpRecvData&& data) {
  uint32_t dest = worker_index_;

  // For concurrency == 1, the packet will always go to the current worker.
  if (concurrency_ > 1) {
    dest = destination(data);
    ASSERT(dest < concurrency_);
  }

  if (dest == worker_index_) {
    onDataWorker(std::move(data));
  } else {
    config_->udpListenerWorkerRouter()->get().deliver(dest, std::move(data));
  }
}

ActiveRawUdpListener::ActiveRawUdpListener(uint32_t worker_index, uint32_t concurrency,
                                           Network::ConnectionHandler& parent,
                                           Event::Dispatcher& dispatcher,
                                           Network::ListenerConfig& config)
    : ActiveRawUdpListener(worker_index, concurrency, parent,
                           config.listenSocketFactory().getListenSocket(), dispatcher, config) {}

ActiveRawUdpListener::ActiveRawUdpListener(uint32_t worker_index, uint32_t concurrency,
                                           Network::ConnectionHandler& parent,
                                           Network::SocketSharedPtr listen_socket_ptr,
                                           Event::Dispatcher& dispatcher,
                                           Network::ListenerConfig& config)
    : ActiveRawUdpListener(worker_index, concurrency, parent, *listen_socket_ptr, listen_socket_ptr,
                           dispatcher, config) {}

ActiveRawUdpListener::ActiveRawUdpListener(uint32_t worker_index, uint32_t concurrency,
                                           Network::ConnectionHandler& parent,
                                           Network::Socket& listen_socket,
                                           Network::SocketSharedPtr listen_socket_ptr,
                                           Event::Dispatcher& dispatcher,
                                           Network::ListenerConfig& config)
    : ActiveRawUdpListener(worker_index, concurrency, parent, listen_socket,
                           dispatcher.createUdpListener(listen_socket_ptr, *this), config) {}

ActiveRawUdpListener::ActiveRawUdpListener(uint32_t worker_index, uint32_t concurrency,
                                           Network::ConnectionHandler& parent,
                                           Network::Socket& listen_socket,
                                           Network::UdpListenerPtr&& listener,
                                           Network::ListenerConfig& config)
    : ActiveUdpListenerBase(worker_index, concurrency, parent, listen_socket, std::move(listener),
                            &config),
      read_filter_(nullptr) {
  // Create the filter chain on creating a new udp listener
  config_->filterChainFactory().createUdpListenerFilterChain(*this, *this);

  // If filter is nullptr, fail the creation of the listener
  if (read_filter_ == nullptr) {
    throw Network::CreateListenerException(
        fmt::format("Cannot create listener as no read filter registered for the udp listener: {} ",
                    config_->name()));
  }

  // Create udp_packet_writer
  udp_packet_writer_ = config.udpPacketWriterFactory()->get().createUdpPacketWriter(
      listen_socket_.ioHandle(), config.listenerScope());
}

void ActiveRawUdpListener::onDataWorker(Network::UdpRecvData&& data) { read_filter_->onData(data); }

void ActiveRawUdpListener::onReadReady() {}

void ActiveRawUdpListener::onWriteReady(const Network::Socket&) {
  // TODO(sumukhs): This is not used now. When write filters are implemented, this is a
  // trigger to invoke the on write ready API on the filters which is when they can write
  // data

  // Clear write_blocked_ status for udpPacketWriter
  udp_packet_writer_->setWritable();
}

void ActiveRawUdpListener::onReceiveError(Api::IoError::IoErrorCode error_code) {
  read_filter_->onReceiveError(error_code);
}

void ActiveRawUdpListener::addReadFilter(Network::UdpListenerReadFilterPtr&& filter) {
  ASSERT(read_filter_ == nullptr, "Cannot add a 2nd UDP read filter");
  read_filter_ = std::move(filter);
}

Network::UdpListener& ActiveRawUdpListener::udpListener() { return *udp_listener_; }

} // namespace Server
} // namespace Envoy
