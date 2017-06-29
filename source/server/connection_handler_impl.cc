#include "server/connection_handler_impl.h"

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/network/filter.h"

namespace Envoy {
namespace Server {

ConnectionHandlerImpl::ConnectionHandlerImpl(spdlog::logger& logger, Event::Dispatcher& dispatcher)
    : logger_(logger), dispatcher_(dispatcher) {}

void ConnectionHandlerImpl::addListener(Network::FilterChainFactory& factory,
                                        Network::ListenSocket& socket, Stats::Scope& scope,
                                        const Network::ListenerOptions& listener_options) {
  ActiveListenerPtr l(new ActiveListener(*this, socket, factory, scope, listener_options));
  listeners_.emplace_back(socket.localAddress(), std::move(l));
}

void ConnectionHandlerImpl::addSslListener(Network::FilterChainFactory& factory,
                                           Ssl::ServerContext& ssl_ctx,
                                           Network::ListenSocket& socket, Stats::Scope& scope,
                                           const Network::ListenerOptions& listener_options) {
  ActiveListenerPtr l(
      new SslActiveListener(*this, ssl_ctx, socket, factory, scope, listener_options));
  listeners_.emplace_back(socket.localAddress(), std::move(l));
}

void ConnectionHandlerImpl::removeListener(Network::ListenSocket& socket) {
  for (auto listener = listeners_.begin(); listener != listeners_.end(); ++listener) {
    if (&listener->second->listener_->socket() == &socket) {
      listeners_.erase(listener);
      break;
    }
  }
}

void ConnectionHandlerImpl::stopListeners() {
  for (auto& listener : listeners_) {
    listener.second->listener_.reset();
  }
}

void ConnectionHandlerImpl::ActiveListener::removeConnection(ActiveConnection& connection) {
  ENVOY_CONN_LOG_TO_LOGGER(parent_.logger_, info, "adding to cleanup list",
                           *connection.connection_);
  ActiveConnectionPtr removed = connection.removeFromList(connections_);
  parent_.dispatcher_.deferredDelete(std::move(removed));
  ASSERT(parent_.num_connections_ > 0);
  parent_.num_connections_--;
}

ConnectionHandlerImpl::ActiveListener::ActiveListener(
    ConnectionHandlerImpl& parent, Network::ListenSocket& socket,
    Network::FilterChainFactory& factory, Stats::Scope& scope,
    const Network::ListenerOptions& listener_options)
    : ActiveListener(
          parent, parent.dispatcher_.createListener(parent, socket, *this, scope, listener_options),
          factory, scope) {}

ConnectionHandlerImpl::ActiveListener::ActiveListener(ConnectionHandlerImpl& parent,
                                                      Network::ListenerPtr&& listener,
                                                      Network::FilterChainFactory& factory,
                                                      Stats::Scope& scope)
    : parent_(parent), factory_(factory), stats_(generateStats(scope)) {
  listener_ = std::move(listener);
}

ConnectionHandlerImpl::ActiveListener::~ActiveListener() {
  while (!connections_.empty()) {
    connections_.front()->connection_->close(Network::ConnectionCloseType::NoFlush);
  }

  parent_.dispatcher_.clearDeferredDeleteList();
}

ConnectionHandlerImpl::SslActiveListener::SslActiveListener(
    ConnectionHandlerImpl& parent, Ssl::ServerContext& ssl_ctx, Network::ListenSocket& socket,
    Network::FilterChainFactory& factory, Stats::Scope& scope,
    const Network::ListenerOptions& listener_options)
    : ActiveListener(parent, parent.dispatcher_.createSslListener(parent, ssl_ctx, socket, *this,
                                                                  scope, listener_options),
                     factory, scope) {}

Network::Listener*
ConnectionHandlerImpl::findListenerByAddress(const Network::Address::Instance& address) {
  // This is a linear operation, may need to add a map<address, listener> to improve performance.
  // However, linear performance might be adequate since the number of listeners is small.
  auto listener = std::find_if(
      listeners_.begin(), listeners_.end(),
      [&address](const std::pair<Network::Address::InstanceConstSharedPtr, ActiveListenerPtr>& p) {
        return p.first->type() == Network::Address::Type::Ip && *(p.first) == address;
      });

  // If there is exact address match, return the corresponding listener.
  if (listener != listeners_.end()) {
    return listener->second->listener_.get();
  }

  // Otherwise, we need to look for the wild card match, i.e., 0.0.0.0:[address_port].
  // TODO(wattli): consolidate with previous search for more efficiency.
  listener = std::find_if(
      listeners_.begin(), listeners_.end(),
      [&address](const std::pair<Network::Address::InstanceConstSharedPtr, ActiveListenerPtr>& p) {
        return p.first->type() == Network::Address::Type::Ip &&
               p.first->ip()->port() == address.ip()->port() && p.first->ip()->isAnyAddress();
      });
  return (listener != listeners_.end()) ? listener->second->listener_.get() : nullptr;
}

void ConnectionHandlerImpl::ActiveListener::onNewConnection(
    Network::ConnectionPtr&& new_connection) {
  ENVOY_CONN_LOG_TO_LOGGER(parent_.logger_, info, "new connection", *new_connection);
  bool empty_filter_chain = !factory_.createFilterChain(*new_connection);

  // If the connection is already closed, we can just let this connection immediately die.
  if (new_connection->state() != Network::Connection::State::Closed) {
    // Close the connection if the filter chain is empty to avoid leaving open connections
    // with nothing to do.
    if (empty_filter_chain) {
      ENVOY_CONN_LOG_TO_LOGGER(parent_.logger_, info, "closing connection: no filters",
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
      conn_length_(listener_.stats_.downstream_cx_length_ms_.allocateSpan()) {
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
  return {ALL_LISTENER_STATS(POOL_COUNTER(scope), POOL_GAUGE(scope), POOL_TIMER(scope))};
}

} // Server
} // Envoy
