#include "server/connection_handler_impl.h"

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/timespan.h"

#include "common/network/connection_impl.h"
#include "common/network/utility.h"
#include "common/stats/timespan_impl.h"

#include "extensions/transport_sockets/well_known_names.h"

namespace Envoy {
namespace Server {

ConnectionHandlerImpl::ConnectionHandlerImpl(Event::Dispatcher& dispatcher,
                                             const std::string& per_handler_stat_prefix)
    : dispatcher_(dispatcher), per_handler_stat_prefix_(per_handler_stat_prefix + "."),
      disable_listeners_(false) {}

void ConnectionHandlerImpl::incNumConnections() { ++num_handler_connections_; }

void ConnectionHandlerImpl::decNumConnections() {
  ASSERT(num_handler_connections_ > 0);
  --num_handler_connections_;
}

void ConnectionHandlerImpl::addListener(Network::ListenerConfig& config) {
  ActiveListenerDetails details;
  if (config.socket().socketType() == Network::Address::SocketType::Stream) {
    auto tcp_listener = std::make_unique<ActiveTcpListener>(*this, config);
    details.tcp_listener_ = *tcp_listener;
    details.listener_ = std::move(tcp_listener);
  } else {
    ASSERT(config.udpListenerFactory() != nullptr, "UDP listener factory is not initialized.");
    details.listener_ =
        config.udpListenerFactory()->createActiveUdpListener(*this, dispatcher_, config);
  }

  if (disable_listeners_) {
    details.listener_->listener()->disable();
  }

  listeners_.emplace_back(config.socket().localAddress(), std::move(details));
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

void ConnectionHandlerImpl::stopListeners(uint64_t listener_tag) {
  for (auto& listener : listeners_) {
    if (listener.second.listener_->listenerTag() == listener_tag) {
      listener.second.listener_->destroy();
    }
  }
}

void ConnectionHandlerImpl::stopListeners() {
  for (auto& listener : listeners_) {
    listener.second.listener_->destroy();
  }
}

void ConnectionHandlerImpl::disableListeners() {
  disable_listeners_ = true;
  for (auto& listener : listeners_) {
    listener.second.listener_->listener()->disable();
  }
}

void ConnectionHandlerImpl::enableListeners() {
  disable_listeners_ = false;
  for (auto& listener : listeners_) {
    listener.second.listener_->listener()->enable();
  }
}

void ConnectionHandlerImpl::ActiveTcpListener::removeConnection(ActiveTcpConnection& connection) {
  ENVOY_CONN_LOG(debug, "adding to cleanup list", *connection.connection_);
  ActiveTcpConnectionPtr removed = connection.removeFromList(connections_);
  parent_.dispatcher_.deferredDelete(std::move(removed));
}

ConnectionHandlerImpl::ActiveListenerImplBase::ActiveListenerImplBase(
    Network::ConnectionHandler& parent, Network::ListenerConfig& config)
    : stats_({ALL_LISTENER_STATS(POOL_COUNTER(config.listenerScope()),
                                 POOL_GAUGE(config.listenerScope()),
                                 POOL_HISTOGRAM(config.listenerScope()))}),
      per_worker_stats_({ALL_PER_HANDLER_LISTENER_STATS(
          POOL_COUNTER_PREFIX(config.listenerScope(), parent.statPrefix()),
          POOL_GAUGE_PREFIX(config.listenerScope(), parent.statPrefix()))}),
      config_(config) {}

ConnectionHandlerImpl::ActiveTcpListener::ActiveTcpListener(ConnectionHandlerImpl& parent,
                                                            Network::ListenerConfig& config)
    : ActiveTcpListener(
          parent, parent.dispatcher_.createListener(config.socket(), *this, config.bindToPort()),
          config) {}

ConnectionHandlerImpl::ActiveTcpListener::ActiveTcpListener(ConnectionHandlerImpl& parent,
                                                            Network::ListenerPtr&& listener,
                                                            Network::ListenerConfig& config)
    : ConnectionHandlerImpl::ActiveListenerImplBase(parent, config), parent_(parent),
      listener_(std::move(listener)), listener_filters_timeout_(config.listenerFiltersTimeout()),
      continue_on_listener_filters_timeout_(config.continueOnListenerFiltersTimeout()) {
  config.connectionBalancer().registerHandler(*this);
}

ConnectionHandlerImpl::ActiveTcpListener::~ActiveTcpListener() {
  config_.connectionBalancer().unregisterHandler(*this);

  // Purge sockets that have not progressed to connections. This should only happen when
  // a listener filter stops iteration and never resumes.
  while (!sockets_.empty()) {
    ActiveTcpSocketPtr removed = sockets_.front()->removeFromList(sockets_);
    parent_.dispatcher_.deferredDelete(std::move(removed));
  }

  while (!connections_.empty()) {
    connections_.front()->connection_->close(Network::ConnectionCloseType::NoFlush);
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
      [&address](
          const std::pair<Network::Address::InstanceConstSharedPtr, ActiveListenerDetails>& p) {
        return p.second.tcp_listener_.has_value() && p.second.listener_->listener() != nullptr &&
               p.first->type() == Network::Address::Type::Ip && *(p.first) == address;
      });

  // If there is exact address match, return the corresponding listener.
  if (listener_it != listeners_.end()) {
    return listener_it->second.tcp_listener_;
  }

  // Otherwise, we need to look for the wild card match, i.e., 0.0.0.0:[address_port].
  // We do not return stopped listeners.
  // TODO(wattli): consolidate with previous search for more efficiency.
  listener_it = std::find_if(
      listeners_.begin(), listeners_.end(),
      [&address](
          const std::pair<Network::Address::InstanceConstSharedPtr, ActiveListenerDetails>& p) {
        return p.second.tcp_listener_.has_value() && p.second.listener_->listener() != nullptr &&
               p.first->type() == Network::Address::Type::Ip &&
               p.first->ip()->port() == address.ip()->port() && p.first->ip()->isAnyAddress();
      });
  return (listener_it != listeners_.end()) ? listener_it->second.tcp_listener_ : absl::nullopt;
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

void ConnectionHandlerImpl::ActiveTcpSocket::newConnection() {
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
    // Create a new connection on this listener.
    listener_.newConnection(std::move(socket_));
  }
}

void ConnectionHandlerImpl::ActiveTcpListener::onAccept(Network::ConnectionSocketPtr&& socket) {
  onAcceptWorker(std::move(socket), config_.handOffRestoredDestinationConnections(), false);
}

void ConnectionHandlerImpl::ActiveTcpListener::onAcceptWorker(
    Network::ConnectionSocketPtr&& socket, bool hand_off_restored_destination_connections,
    bool rebalanced) {
  if (!rebalanced) {
    Network::BalancedConnectionHandler& target_handler =
        config_.connectionBalancer().pickTargetHandler(*this);
    if (&target_handler != this) {
      target_handler.post(std::move(socket));
      return;
    }
  }

  auto active_socket = std::make_unique<ActiveTcpSocket>(*this, std::move(socket),
                                                         hand_off_restored_destination_connections);

  // Create and run the filters
  config_.filterChainFactory().createListenerFilterChain(*active_socket);
  active_socket->continueFilterChain(true);

  // Move active_socket to the sockets_ list if filter iteration needs to continue later.
  // Otherwise we let active_socket be destructed when it goes out of scope.
  if (active_socket->iter_ != active_socket->accept_filters_.end()) {
    active_socket->startTimer();
    active_socket->moveIntoListBack(std::move(active_socket), sockets_);
  }
}

void ConnectionHandlerImpl::ActiveTcpListener::newConnection(
    Network::ConnectionSocketPtr&& socket) {
  // Find matching filter chain.
  const auto filter_chain = config_.filterChainManager().findFilterChain(*socket);
  if (filter_chain == nullptr) {
    ENVOY_LOG(debug, "closing connection: no matching filter chain found");
    stats_.no_filter_chain_match_.inc();
    socket->close();
    return;
  }

  auto transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  ActiveTcpConnectionPtr active_connection(new ActiveTcpConnection(
      *this,
      parent_.dispatcher_.createServerConnection(std::move(socket), std::move(transport_socket)),
      parent_.dispatcher_.timeSource()));
  active_connection->connection_->setBufferLimits(config_.perConnectionBufferLimitBytes());

  const bool empty_filter_chain = !config_.filterChainFactory().createNetworkFilterChain(
      *active_connection->connection_, filter_chain->networkFilterFactories());
  if (empty_filter_chain) {
    ENVOY_CONN_LOG(debug, "closing connection: no filters", *active_connection->connection_);
    active_connection->connection_->close(Network::ConnectionCloseType::NoFlush);
  }

  // If the connection is already closed, we can just let this connection immediately die.
  if (active_connection->connection_->state() != Network::Connection::State::Closed) {
    ENVOY_CONN_LOG(debug, "new connection", *active_connection->connection_);
    active_connection->connection_->addConnectionCallbacks(*active_connection);
    active_connection->moveIntoList(std::move(active_connection), connections_);
  }
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

  parent_.dispatcher_.post([socket_to_rebalance, tag = config_.listenerTag(), &parent = parent_]() {
    // TODO(mattklein123): We should probably use a hash table here to lookup the tag instead of
    // iterating through the listener list.
    for (const auto& listener : parent.listeners_) {
      if (listener.second.listener_->listener() != nullptr &&
          listener.second.listener_->listenerTag() == tag) {
        // If the tag matches this must be a TCP listener.
        ASSERT(listener.second.tcp_listener_.has_value());
        listener.second.tcp_listener_.value().get().onAcceptWorker(
            std::move(socket_to_rebalance->socket),
            listener.second.tcp_listener_.value()
                .get()
                .config_.handOffRestoredDestinationConnections(),
            true);
        return;
      }
    }
  });
}

ConnectionHandlerImpl::ActiveTcpConnection::ActiveTcpConnection(
    ActiveTcpListener& listener, Network::ConnectionPtr&& new_connection, TimeSource& time_source)
    : listener_(listener), connection_(std::move(new_connection)),
      conn_length_(new Stats::HistogramCompletableTimespanImpl(
          listener_.stats_.downstream_cx_length_ms_, time_source)) {
  // We just universally set no delay on connections. Theoretically we might at some point want
  // to make this configurable.
  connection_->noDelay(true);

  listener_.stats_.downstream_cx_total_.inc();
  listener_.stats_.downstream_cx_active_.inc();
  listener_.per_worker_stats_.downstream_cx_total_.inc();
  listener_.per_worker_stats_.downstream_cx_active_.inc();

  // Active connections on the handler (not listener). The per listener connections have already
  // been incremented at this point either via the connection balancer or in the socket accept
  // path if there is no configured balancer.
  ++listener_.parent_.num_handler_connections_;
}

ConnectionHandlerImpl::ActiveTcpConnection::~ActiveTcpConnection() {
  listener_.stats_.downstream_cx_active_.dec();
  listener_.stats_.downstream_cx_destroy_.inc();
  listener_.per_worker_stats_.downstream_cx_active_.dec();
  conn_length_->complete();

  // Active listener connections (not handler).
  listener_.decNumConnections();

  // Active handler connections (not listener).
  listener_.parent_.decNumConnections();
}

ActiveUdpListener::ActiveUdpListener(Network::ConnectionHandler& parent,
                                     Event::Dispatcher& dispatcher, Network::ListenerConfig& config)
    : ActiveUdpListener(parent, dispatcher.createUdpListener(config.socket(), *this), config) {}

ActiveUdpListener::ActiveUdpListener(Network::ConnectionHandler& parent,
                                     Network::UdpListenerPtr&& listener,
                                     Network::ListenerConfig& config)
    : ConnectionHandlerImpl::ActiveListenerImplBase(parent, config),
      udp_listener_(std::move(listener)), read_filter_(nullptr) {
  // Create the filter chain on creating a new udp listener
  config_.filterChainFactory().createUdpListenerFilterChain(*this, *this);

  // If filter is nullptr, fail the creation of the listener
  if (read_filter_ == nullptr) {
    throw Network::CreateListenerException(
        fmt::format("Cannot create listener as no read filter registered for the udp listener: {} ",
                    config_.name()));
  }
}

void ActiveUdpListener::onData(Network::UdpRecvData& data) { read_filter_->onData(data); }

void ActiveUdpListener::onWriteReady(const Network::Socket&) {
  // TODO(sumukhs): This is not used now. When write filters are implemented, this is a
  // trigger to invoke the on write ready API on the filters which is when they can write
  // data
}

void ActiveUdpListener::onReceiveError(const Network::UdpListenerCallbacks::ErrorCode&,
                                       Api::IoError::IoErrorCode) {
  // TODO(sumukhs): Determine what to do on receive error.
  // Would the filters need to know on error? Can't foresee a scenario where they
  // would take an action
}

void ActiveUdpListener::addReadFilter(Network::UdpListenerReadFilterPtr&& filter) {
  ASSERT(read_filter_ == nullptr, "Cannot add a 2nd UDP read filter");
  read_filter_ = std::move(filter);
}

Network::UdpListener& ActiveUdpListener::udpListener() { return *udp_listener_; }

} // namespace Server
} // namespace Envoy
