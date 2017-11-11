#include "server/connection_handler_impl.h"

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/network/filter.h"
#include "envoy/stats/timespan.h"

namespace Envoy {
namespace Server {

ConnectionHandlerImpl::ConnectionHandlerImpl(spdlog::logger& logger, Event::Dispatcher& dispatcher)
    : logger_(logger), dispatcher_(dispatcher) {}

void ConnectionHandlerImpl::addListener(Network::FilterChainFactory& factory,
                                        Network::ListenSocket& socket, Stats::Scope& scope,
                                        uint64_t listener_tag,
                                        const Network::ListenerOptions& listener_options) {
  ActiveListenerPtr l(
      new ActiveListener(*this, socket, factory, scope, listener_tag, listener_options));
  listeners_.emplace_back(socket.localAddress(), std::move(l));
}

void ConnectionHandlerImpl::addSslListener(Network::FilterChainFactory& factory,
                                           Ssl::ServerContext& ssl_ctx,
                                           Network::ListenSocket& socket, Stats::Scope& scope,
                                           uint64_t listener_tag,
                                           const Network::ListenerOptions& listener_options) {
  ActiveListenerPtr l(new SslActiveListener(*this, ssl_ctx, socket, factory, scope, listener_tag,
                                            listener_options));
  listeners_.emplace_back(socket.localAddress(), std::move(l));
}

void ConnectionHandlerImpl::removeListeners(uint64_t listener_tag) {
  for (auto listener = listeners_.begin(); listener != listeners_.end();) {
    if (listener->second->listener_tag_ == listener_tag) {
      listener = listeners_.erase(listener);
    } else {
      ++listener;
    }
  }
}

void ConnectionHandlerImpl::stopListeners(uint64_t listener_tag) {
  for (auto& listener : listeners_) {
    if (listener.second->listener_tag_ == listener_tag) {
      listener.second->listener_.reset();
    }
  }
}

void ConnectionHandlerImpl::stopListeners() {
  for (auto& listener : listeners_) {
    listener.second->listener_.reset();
  }
}

void ConnectionHandlerImpl::ActiveListener::removeConnection(ActiveConnection& connection) {
  ENVOY_CONN_LOG_TO_LOGGER(parent_.logger_, debug, "adding to cleanup list",
                           *connection.connection_);
  ActiveConnectionPtr removed = connection.removeFromList(connections_);
  parent_.dispatcher_.deferredDelete(std::move(removed));
  ASSERT(parent_.num_connections_ > 0);
  parent_.num_connections_--;
}

ConnectionHandlerImpl::ActiveListener::ActiveListener(
    ConnectionHandlerImpl& parent, Network::ListenSocket& socket,
    Network::FilterChainFactory& factory, Stats::Scope& scope, uint64_t listener_tag,
    const Network::ListenerOptions& listener_options)
    : ActiveListener(
          parent, parent.dispatcher_.createListener(parent, socket, *this, scope, listener_options),
          factory, scope, listener_tag) {}

ConnectionHandlerImpl::ActiveListener::ActiveListener(ConnectionHandlerImpl& parent,
                                                      Network::ListenerPtr&& listener,
                                                      Network::FilterChainFactory& factory,
                                                      Stats::Scope& scope, uint64_t listener_tag)
    : parent_(parent), factory_(factory), listener_(std::move(listener)),
      stats_(generateStats(scope)), listener_tag_(listener_tag) {}

ConnectionHandlerImpl::ActiveListener::~ActiveListener() {
  while (!connections_.empty()) {
    connections_.front()->connection_->close(Network::ConnectionCloseType::NoFlush);
  }

  parent_.dispatcher_.clearDeferredDeleteList();
}

ConnectionHandlerImpl::SslActiveListener::SslActiveListener(
    ConnectionHandlerImpl& parent, Ssl::ServerContext& ssl_ctx, Network::ListenSocket& socket,
    Network::FilterChainFactory& factory, Stats::Scope& scope, uint64_t listener_tag,
    const Network::ListenerOptions& listener_options)
    : ActiveListener(parent,
                     parent.dispatcher_.createSslListener(parent, ssl_ctx, socket, *this, scope,
                                                          listener_options),
                     factory, scope, listener_tag) {}

Network::Listener*
ConnectionHandlerImpl::findListenerByAddress(const Network::Address::Instance& address) {
  // This is a linear operation, may need to add a map<address, listener> to improve performance.
  // However, linear performance might be adequate since the number of listeners is small.
  // We do not return stopped listeners.
  auto listener = std::find_if(
      listeners_.begin(), listeners_.end(),
      [&address](const std::pair<Network::Address::InstanceConstSharedPtr, ActiveListenerPtr>& p) {
        return p.second->listener_ != nullptr && p.first->type() == Network::Address::Type::Ip &&
               *(p.first) == address;
      });

  // If there is exact address match, return the corresponding listener.
  if (listener != listeners_.end()) {
    return listener->second->listener_.get();
  }

  // Otherwise, we need to look for the wild card match, i.e., 0.0.0.0:[address_port].
  // We do not return stopped listeners.
  // TODO(wattli): consolidate with previous search for more efficiency.
  listener = std::find_if(
      listeners_.begin(), listeners_.end(),
      [&address](const std::pair<Network::Address::InstanceConstSharedPtr, ActiveListenerPtr>& p) {
        return p.second->listener_ != nullptr && p.first->type() == Network::Address::Type::Ip &&
               p.first->ip()->port() == address.ip()->port() && p.first->ip()->isAnyAddress();
      });
  return (listener != listeners_.end()) ? listener->second->listener_.get() : nullptr;
}

void ConnectionHandlerImpl::ActiveListener::onNewConnection(
    Network::ConnectionPtr&& new_connection) {
  ENVOY_CONN_LOG_TO_LOGGER(parent_.logger_, debug, "new connection", *new_connection);
  bool empty_filter_chain = !factory_.createFilterChain(*new_connection);

  // If the connection is already closed, we can just let this connection immediately die.
  if (new_connection->state() != Network::Connection::State::Closed) {
    // Close the connection if the filter chain is empty to avoid leaving open connections
    // with nothing to do.
    if (empty_filter_chain) {
      ENVOY_CONN_LOG_TO_LOGGER(parent_.logger_, debug, "closing connection: no filters",
                               *new_connection);
      new_connection->close(Network::ConnectionCloseType::NoFlush);
    } else {
      ActiveConnectionPtr active_connection(new ActiveConnection(*this, std::move(new_connection)));
      active_connection->moveIntoList(std::move(active_connection), connections_);
      parent_.num_connections_++;
    }
  }
}

ConnectionHandlerImpl::ActiveConnection::ActiveConnection(ActiveListener& listener,
                                                          Network::ConnectionPtr&& new_connection)
    : listener_(listener), connection_(std::move(new_connection)),
      conn_length_(new Stats::Timespan(listener_.stats_.downstream_cx_length_ms_)) {
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

} // namespace Server
} // namespace Envoy
