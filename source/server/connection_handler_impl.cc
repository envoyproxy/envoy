#include "server/connection_handler_impl.h"

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/network/filter.h"
#include "envoy/stats/timespan.h"

#include "common/filter/original_dst.h"
#include "common/filter/proxy_protocol.h"
#include "common/network/connection_impl.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Server {

ConnectionHandlerImpl::ConnectionHandlerImpl(spdlog::logger& logger, Event::Dispatcher& dispatcher)
    : logger_(logger), dispatcher_(dispatcher) {}

void ConnectionHandlerImpl::addListener(Listener& config) {
  ActiveListenerPtr l(new ActiveListener(*this, config));
  listeners_.emplace_back(config.socket().localAddress(), std::move(l));
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

ConnectionHandlerImpl::ActiveListener::ActiveListener(ConnectionHandlerImpl& parent,
                                                      Listener& config)
    : ActiveListener(parent,
                     parent.dispatcher_.createListener(config.socket(), *this, config.bindToPort()),
                     config) {}

ConnectionHandlerImpl::ActiveListener::ActiveListener(ConnectionHandlerImpl& parent,
                                                      Network::ListenerPtr&& listener,
                                                      Listener& config)
    : parent_(parent), listener_(std::move(listener)),
      stats_(generateStats(config.listenerScope())), listener_tag_(config.listenerTag()),
      config_(config), legacy_stats_(new Filter::ProxyProtocol::Config(config.listenerScope())) {}

ConnectionHandlerImpl::ActiveListener::~ActiveListener() {
  while (!sockets_.empty()) {
    ActiveSocketPtr removed = sockets_.front()->removeFromList(sockets_);
    removed->socket_->close();
    parent_.dispatcher_.deferredDelete(std::move(removed));
  }

  while (!connections_.empty()) {
    connections_.front()->connection_->close(Network::ConnectionCloseType::NoFlush);
  }

  parent_.dispatcher_.clearDeferredDeleteList();
}

Network::Listener*
ConnectionHandlerImpl::findListenerByAddress(const Network::Address::Instance& address) {
  ActiveListener* listener = findActiveListenerByAddress(address);
  return listener ? listener->listener_.get() : nullptr;
}

ConnectionHandlerImpl::ActiveListener*
ConnectionHandlerImpl::findActiveListenerByAddress(const Network::Address::Instance& address) {
  // This is a linear operation, may need to add a map<address, listener> to improve performance.
  // However, linear performance might be adequate since the number of listeners is small.
  // We do not return stopped listeners.
  auto iter = std::find_if(
      listeners_.begin(), listeners_.end(),
      [&address](const std::pair<Network::Address::InstanceConstSharedPtr, ActiveListenerPtr>& p) {
        return p.second->listener_ != nullptr && p.first->type() == Network::Address::Type::Ip &&
               *(p.first) == address;
      });

  // If there is exact address match, return the corresponding listener.
  if (iter != listeners_.end()) {
    return iter->second.get();
  }

  // Otherwise, we need to look for the wild card match, i.e., 0.0.0.0:[address_port].
  // We do not return stopped listeners.
  // TODO(wattli): consolidate with previous search for more efficiency.
  iter = std::find_if(
      listeners_.begin(), listeners_.end(),
      [&address](const std::pair<Network::Address::InstanceConstSharedPtr, ActiveListenerPtr>& p) {
        return p.second->listener_ != nullptr && p.first->type() == Network::Address::Type::Ip &&
               p.first->ip()->port() == address.ip()->port() && p.first->ip()->isAnyAddress();
      });
  return (iter != listeners_.end()) ? iter->second.get() : nullptr;
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
        return;
      }
      // Check if another listener may have to be used.
      if (socket_->localAddressReset()) {
        // Hands off redirected connections (from iptables) to the listener associated with the
        // original destination address. If there is no listener associated with the original
        // destination address, the connection is handled by the listener that receives it.
        ActiveListener* new_listener =
            listener_->parent_.findActiveListenerByAddress(*socket_->localAddress());

        if (new_listener != nullptr) {
          // Reset the accept socket transiet state and hand it to the new listener.
          socket_->clearReset();
          new_listener->onAccept(std::move(socket_));
          goto out;
        }
      }
    }
    // Successfully ran all the accept filters, create a new connection.
    listener_->newConnection(std::move(socket_));
  } else {
    // current filter failed, connection must be abandoned.
    socket_->close();
  }
out:
  // Filter execution concluded, clear state.
  iter_ = accept_filters_.end();
  ActiveSocketPtr removed = removeFromList(listener_->sockets_);
  listener_->parent_.dispatcher_.deferredDelete(std::move(removed));
}

void ConnectionHandlerImpl::ActiveListener::onAccept(Network::AcceptSocketPtr&& accept_socket) {
  Network::Address::InstanceConstSharedPtr local_address = accept_socket->localAddress();
  ActiveSocket* active_socket(new ActiveSocket(*this, std::move(accept_socket)));

  // Implicitly add legacy filters
  if (config_.useOriginalDst()) {
    active_socket->accept_filters_.emplace_back(
        Network::ListenerFilterSharedPtr{new Filter::OriginalDst()});
  }
  if (config_.useProxyProto()) {
    active_socket->accept_filters_.emplace_back(
        Network::ListenerFilterSharedPtr{new Filter::ProxyProtocol::Instance(legacy_stats_)});
  }

  // Create and run the filters
  config_.filterChainFactory().createFilterChain(*active_socket);
  ActiveSocketPtr as(active_socket);
  as->moveIntoListBack(std::move(as), sockets_);
  active_socket->continueFilterChain(true);
}

void ConnectionHandlerImpl::ActiveListener::newConnection(
    Network::AcceptSocketPtr&& accept_socket) {
  Network::ConnectionPtr new_connection =
      parent_.dispatcher_.createConnection(std::move(accept_socket), config_.defaultSslContext());
  new_connection->setBufferLimits(config_.perConnectionBufferLimitBytes());
  onNewConnection(std::move(new_connection));
}

void ConnectionHandlerImpl::ActiveListener::onNewConnection(
    Network::ConnectionPtr&& new_connection) {
  ENVOY_CONN_LOG_TO_LOGGER(parent_.logger_, debug, "new connection", *new_connection);
  bool empty_filter_chain = !config_.filterChainFactory().createFilterChain(*new_connection);

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
