#include "server/connection_handler_impl.h"

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/network/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/timespan.h"

#include "common/network/connection_impl.h"
#include "common/network/utility.h"

#include "extensions/transport_sockets/well_known_names.h"

namespace Envoy {
namespace Server {

ConnectionHandlerImpl::ConnectionHandlerImpl(spdlog::logger& logger, Event::Dispatcher& dispatcher)
    : logger_(logger), dispatcher_(dispatcher), disable_listeners_(false) {}

void ConnectionHandlerImpl::addListener(Network::ListenerConfig& config) {
  Network::ConnectionHandler::ActiveListenerPtr listener;
  Network::Address::SocketType socket_type = config.socket().socketType();

  if (socket_type == Network::Address::SocketType::Stream) {
    listener = std::make_unique<ActiveTcpListener>(*this, config);
  } else {
    ASSERT(socket_type == Network::Address::SocketType::Datagram,
           "Only datagram/stream listener supported");
    listener =
        config.udpListenerFactory()->createActiveUdpListener(*this, dispatcher_, logger_, config);
  }

  if (disable_listeners_) {
    listener->listener()->disable();
  }
  listeners_.emplace_back(config.socket().localAddress(), std::move(listener));
}

void ConnectionHandlerImpl::removeListeners(uint64_t listener_tag) {
  for (auto listener = listeners_.begin(); listener != listeners_.end();) {
    if (listener->second->listenerTag() == listener_tag) {
      listener = listeners_.erase(listener);
    } else {
      ++listener;
    }
  }
}

void ConnectionHandlerImpl::stopListeners(uint64_t listener_tag) {
  for (auto& listener : listeners_) {
    if (listener.second->listenerTag() == listener_tag) {
      listener.second->destroy();
    }
  }
}

void ConnectionHandlerImpl::stopListeners() {
  for (auto& listener : listeners_) {
    listener.second->destroy();
  }
}

void ConnectionHandlerImpl::disableListeners() {
  disable_listeners_ = true;
  for (auto& listener : listeners_) {
    listener.second->listener()->disable();
  }
}

void ConnectionHandlerImpl::enableListeners() {
  disable_listeners_ = false;
  for (auto& listener : listeners_) {
    listener.second->listener()->enable();
  }
}

void ConnectionHandlerImpl::ActiveTcpListener::removeConnection(ActiveConnection& connection) {
  ENVOY_CONN_LOG_TO_LOGGER(parent_.logger_, debug, "adding to cleanup list",
                           *connection.connection_);
  ActiveConnectionPtr removed = connection.removeFromList(connections_);
  parent_.dispatcher_.deferredDelete(std::move(removed));
  ASSERT(parent_.num_connections_ > 0);
  parent_.num_connections_--;
}

ConnectionHandlerImpl::ActiveListenerImplBase::ActiveListenerImplBase(
    Network::ListenerPtr&& listener, Network::ListenerConfig& config)
    : listener_(std::move(listener)), stats_(generateStats(config.listenerScope())),
      listener_filters_timeout_(config.listenerFiltersTimeout()),
      continue_on_listener_filters_timeout_(config.continueOnListenerFiltersTimeout()),
      listener_tag_(config.listenerTag()), config_(config) {}

ConnectionHandlerImpl::ActiveTcpListener::ActiveTcpListener(ConnectionHandlerImpl& parent,
                                                            Network::ListenerConfig& config)
    : ActiveTcpListener(
          parent,
          parent.dispatcher_.createListener(config.socket(), *this, config.bindToPort(),
                                            config.handOffRestoredDestinationConnections()),
          config) {}

ConnectionHandlerImpl::ActiveTcpListener::ActiveTcpListener(ConnectionHandlerImpl& parent,
                                                            Network::ListenerPtr&& listener,
                                                            Network::ListenerConfig& config)
    : ConnectionHandlerImpl::ActiveListenerImplBase(std::move(listener), config), parent_(parent) {}

ConnectionHandlerImpl::ActiveTcpListener::~ActiveTcpListener() {
  // Purge sockets that have not progressed to connections. This should only happen when
  // a listener filter stops iteration and never resumes.
  while (!sockets_.empty()) {
    ActiveSocketPtr removed = sockets_.front()->removeFromList(sockets_);
    parent_.dispatcher_.deferredDelete(std::move(removed));
  }

  while (!connections_.empty()) {
    connections_.front()->connection_->close(Network::ConnectionCloseType::NoFlush);
  }

  parent_.dispatcher_.clearDeferredDeleteList();
}

Network::Listener*
ConnectionHandlerImpl::findListenerByAddress(const Network::Address::Instance& address) {
  Network::ConnectionHandler::ActiveListener* listener = findActiveListenerByAddress(address);
  return listener ? listener->listener() : nullptr;
}

Network::ConnectionHandler::ActiveListener*
ConnectionHandlerImpl::findActiveListenerByAddress(const Network::Address::Instance& address) {
  // This is a linear operation, may need to add a map<address, listener> to improve performance.
  // However, linear performance might be adequate since the number of listeners is small.
  // We do not return stopped listeners.
  auto listener_it =
      std::find_if(listeners_.begin(), listeners_.end(),
                   [&address](const std::pair<Network::Address::InstanceConstSharedPtr,
                                              Network::ConnectionHandler::ActiveListenerPtr>& p) {
                     return p.second->listener() != nullptr &&
                            p.first->type() == Network::Address::Type::Ip && *(p.first) == address;
                   });

  // If there is exact address match, return the corresponding listener.
  if (listener_it != listeners_.end()) {
    return listener_it->second.get();
  }

  // Otherwise, we need to look for the wild card match, i.e., 0.0.0.0:[address_port].
  // We do not return stopped listeners.
  // TODO(wattli): consolidate with previous search for more efficiency.
  listener_it = std::find_if(
      listeners_.begin(), listeners_.end(),
      [&address](const std::pair<Network::Address::InstanceConstSharedPtr,
                                 Network::ConnectionHandler::ActiveListenerPtr>& p) {
        return p.second->listener() != nullptr && p.first->type() == Network::Address::Type::Ip &&
               p.first->ip()->port() == address.ip()->port() && p.first->ip()->isAnyAddress();
      });
  return (listener_it != listeners_.end()) ? listener_it->second.get() : nullptr;
}

void ConnectionHandlerImpl::ActiveSocket::onTimeout() {
  listener_.stats_.downstream_pre_cx_timeout_.inc();
  ASSERT(inserted());
  if (listener_.continue_on_listener_filters_timeout_) {
    newConnection();
  }
  unlink();
}

void ConnectionHandlerImpl::ActiveSocket::startTimer() {
  if (listener_.listener_filters_timeout_.count() > 0) {
    timer_ = listener_.parent_.dispatcher_.createTimer([this]() -> void { onTimeout(); });
    timer_->enableTimer(listener_.listener_filters_timeout_);
  }
}

void ConnectionHandlerImpl::ActiveSocket::unlink() {
  ActiveSocketPtr removed = removeFromList(listener_.sockets_);
  if (removed->timer_ != nullptr) {
    removed->timer_->disableTimer();
  }
  listener_.parent_.dispatcher_.deferredDelete(std::move(removed));
}

void ConnectionHandlerImpl::ActiveSocket::continueFilterChain(bool success) {
  if (success) {
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
        return;
      }
    }
    // Successfully ran all the accept filters.
    newConnection();
  }

  // Filter execution concluded, unlink and delete this ActiveSocket if it was linked.
  if (inserted()) {
    unlink();
  }
}

void ConnectionHandlerImpl::ActiveSocket::newConnection() {
  // Check if the socket may need to be redirected to another listener.
  ConnectionHandler::ActiveListener* new_listener = nullptr;

  if (hand_off_restored_destination_connections_ && socket_->localAddressRestored()) {
    // Find a listener associated with the original destination address.
    new_listener = listener_.parent_.findActiveListenerByAddress(*socket_->localAddress());
  }
  if (new_listener != nullptr) {
    // TODO(sumukhs): Try to avoid dynamic_cast by coming up with a better interface design
    ActiveTcpListener* tcp_listener = dynamic_cast<ActiveTcpListener*>(new_listener);
    ASSERT(tcp_listener != nullptr, "ActiveSocket listener is expected to be tcp");
    // Hands off connections redirected by iptables to the listener associated with the
    // original destination address. Pass 'hand_off_restored_destination_connections' as false to
    // prevent further redirection.
    tcp_listener->onAccept(std::move(socket_),
                           false /* hand_off_restored_destination_connections */);
  } else {
    // Set default transport protocol if none of the listener filters did it.
    if (socket_->detectedTransportProtocol().empty()) {
      socket_->setDetectedTransportProtocol(
          Extensions::TransportSockets::TransportSocketNames::get().RawBuffer);
    }
    // Create a new connection on this listener.
    listener_.newConnection(std::move(socket_));
  }
}

void ConnectionHandlerImpl::ActiveTcpListener::onAccept(
    Network::ConnectionSocketPtr&& socket, bool hand_off_restored_destination_connections) {
  auto active_socket = std::make_unique<ActiveSocket>(*this, std::move(socket),
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
    ENVOY_LOG_TO_LOGGER(parent_.logger_, debug,
                        "closing connection: no matching filter chain found");
    stats_.no_filter_chain_match_.inc();
    socket->close();
    return;
  }

  auto transport_socket = filter_chain->transportSocketFactory().createTransportSocket(nullptr);
  Network::ConnectionPtr new_connection =
      parent_.dispatcher_.createServerConnection(std::move(socket), std::move(transport_socket));
  new_connection->setBufferLimits(config_.perConnectionBufferLimitBytes());

  const bool empty_filter_chain = !config_.filterChainFactory().createNetworkFilterChain(
      *new_connection, filter_chain->networkFilterFactories());
  if (empty_filter_chain) {
    ENVOY_CONN_LOG_TO_LOGGER(parent_.logger_, debug, "closing connection: no filters",
                             *new_connection);
    new_connection->close(Network::ConnectionCloseType::NoFlush);
    return;
  }

  onNewConnection(std::move(new_connection));
}

void ConnectionHandlerImpl::ActiveTcpListener::onNewConnection(
    Network::ConnectionPtr&& new_connection) {
  ENVOY_CONN_LOG_TO_LOGGER(parent_.logger_, debug, "new connection", *new_connection);

  // If the connection is already closed, we can just let this connection immediately die.
  if (new_connection->state() != Network::Connection::State::Closed) {
    ActiveConnectionPtr active_connection(
        new ActiveConnection(*this, std::move(new_connection), parent_.dispatcher_.timeSource()));
    active_connection->moveIntoList(std::move(active_connection), connections_);
    parent_.num_connections_++;
  }
}

ConnectionHandlerImpl::ActiveConnection::ActiveConnection(ActiveTcpListener& listener,
                                                          Network::ConnectionPtr&& new_connection,
                                                          TimeSource& time_source)
    : listener_(listener), connection_(std::move(new_connection)),
      conn_length_(new Stats::Timespan(listener_.stats_.downstream_cx_length_ms_, time_source)) {
  // We just universally set no delay on connections. Theoretically we might at some point want
  // to make this configurable.
  connection_->noDelay(true);
  connection_->addConnectionCallbacks(*this);
  listener_.stats_.downstream_cx_total_.inc();
  listener_.stats_.downstream_cx_active_.inc();
}

ConnectionHandlerImpl::ActiveConnection::~ActiveConnection() {
  listener_.stats_.downstream_cx_active_.dec();
  listener_.stats_.downstream_cx_destroy_.inc();
  conn_length_->complete();
}

ListenerStats ConnectionHandlerImpl::generateStats(Stats::Scope& scope) {
  return {ALL_LISTENER_STATS(POOL_COUNTER(scope), POOL_GAUGE(scope), POOL_HISTOGRAM(scope))};
}

ActiveUdpListener::ActiveUdpListener(Event::Dispatcher& dispatcher, Network::ListenerConfig& config)
    : ActiveUdpListener(dispatcher.createUdpListener(config.socket(), *this), config) {}

ActiveUdpListener::ActiveUdpListener(Network::ListenerPtr&& listener,
                                     Network::ListenerConfig& config)
    : ConnectionHandlerImpl::ActiveListenerImplBase(std::move(listener), config),
      udp_listener_(dynamic_cast<Network::UdpListener*>(listener_.get())), read_filter_(nullptr) {
  // TODO(sumukhs): Try to avoid dynamic_cast by coming up with a better interface design
  ASSERT(udp_listener_ != nullptr, "");

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
