#include "connection_handler_impl.h"

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/network/filter.h"

#include "common/network/listener_impl.h"
#include "common/event/dispatcher_impl.h"

namespace Server {

ConnectionHandlerImpl::ConnectionHandlerImpl(Stats::Store& stats_store, spdlog::logger& logger,
                                             Api::ApiPtr&& api)
    : stats_store_(stats_store), logger_(logger), api_(std::move(api)),
      dispatcher_(api_->allocateDispatcher()),
      watchdog_miss_counter_(stats_store.counter("server.watchdog_miss")),
      watchdog_mega_miss_counter_(stats_store.counter("server.watchdog_mega_miss")) {}

ConnectionHandlerImpl::~ConnectionHandlerImpl() { closeConnections(); }

void ConnectionHandlerImpl::addListener(Network::FilterChainFactory& factory,
                                        Network::ListenSocket& socket, bool bind_to_port,
                                        bool use_proxy_proto, bool use_orig_dst) {
  ActiveListenerPtr l(
      new ActiveListener(*this, socket, factory, bind_to_port, use_proxy_proto, use_orig_dst));
  listeners_.insert(std::make_pair(socket.name(), std::move(l)));
}

void ConnectionHandlerImpl::addSslListener(Network::FilterChainFactory& factory,
                                           Ssl::ServerContext& ssl_ctx,
                                           Network::ListenSocket& socket, bool bind_to_port,
                                           bool use_proxy_proto, bool use_orig_dst) {
  ActiveListenerPtr l(new SslActiveListener(*this, ssl_ctx, socket, factory, bind_to_port,
                                            use_proxy_proto, use_orig_dst));
  listeners_.insert(std::make_pair(socket.name(), std::move(l)));
}

void ConnectionHandlerImpl::closeConnections() {
  while (!connections_.empty()) {
    connections_.front()->connection_->close(Network::ConnectionCloseType::NoFlush);
  }

  dispatcher_->clearDeferredDeleteList();
}

void ConnectionHandlerImpl::closeListeners() {
  for (auto& l : listeners_) {
    l.second->listener_.reset();
  }
}

void ConnectionHandlerImpl::removeConnection(ActiveConnection& connection) {
  conn_log(logger_, info, "adding to cleanup list", *connection.connection_);
  ActiveConnectionPtr removed = connection.removeFromList(connections_);
  dispatcher_->deferredDelete(std::move(removed));
  num_connections_--;
}

ConnectionHandlerImpl::ActiveListener::ActiveListener(ConnectionHandlerImpl& parent,
                                                      Network::ListenSocket& socket,
                                                      Network::FilterChainFactory& factory,
                                                      bool bind_to_port, bool use_proxy_proto,
                                                      bool use_orig_dst)
    : ActiveListener(parent, parent.dispatcher_->createListener(parent, socket, *this,
                                                                parent.stats_store_, bind_to_port,
                                                                use_proxy_proto, use_orig_dst),
                     factory, socket.name()) {}

ConnectionHandlerImpl::ActiveListener::ActiveListener(ConnectionHandlerImpl& parent,
                                                      Network::ListenerPtr&& listener,
                                                      Network::FilterChainFactory& factory,
                                                      const std::string& stats_prefix)
    : parent_(parent), factory_(factory), stats_(generateStats(stats_prefix, parent.stats_store_)) {
  listener_ = std::move(listener);
}

ConnectionHandlerImpl::SslActiveListener::SslActiveListener(ConnectionHandlerImpl& parent,
                                                            Ssl::ServerContext& ssl_ctx,
                                                            Network::ListenSocket& socket,
                                                            Network::FilterChainFactory& factory,
                                                            bool bind_to_port, bool use_proxy_proto,
                                                            bool use_orig_dst)
    : ActiveListener(parent, parent.dispatcher_->createSslListener(
                                 parent, ssl_ctx, socket, *this, parent.stats_store_, bind_to_port,
                                 use_proxy_proto, use_orig_dst),
                     factory, socket.name()) {}

Network::Listener* ConnectionHandlerImpl::findListener(const std::string& socket_name) {
  auto l = listeners_.find(socket_name);
  return (l != listeners_.end()) ? l->second->listener_.get() : nullptr;
}

void ConnectionHandlerImpl::ActiveListener::onNewConnection(
    Network::ConnectionPtr&& new_connection) {
  conn_log(parent_.logger_, info, "new connection", *new_connection);
  bool empty_filter_chain = !factory_.createFilterChain(*new_connection);

  // If the connection is already closed, we can just let this connection immediately die.
  if (new_connection->state() != Network::Connection::State::Closed) {
    // Close the connection if the filter chain is empty to avoid
    // leaving open connections with nothing to do.
    if (empty_filter_chain) {
      conn_log(parent_.logger_, info, "closing connection - no filters", *new_connection);
      new_connection->close(Network::ConnectionCloseType::NoFlush);
    } else {
      ActiveConnectionPtr active_connection(
          new ActiveConnection(parent_, std::move(new_connection), stats_));
      active_connection->moveIntoList(std::move(active_connection), parent_.connections_);
      parent_.num_connections_++;
    }
  }
}

ConnectionHandlerImpl::ActiveConnection::ActiveConnection(ConnectionHandlerImpl& parent,
                                                          Network::ConnectionPtr&& new_connection,
                                                          ListenerStats& stats)
    : parent_(parent), connection_(std::move(new_connection)), stats_(stats),
      conn_length_(stats_.downstream_cx_length_ms_.allocateSpan()) {
  // We just universally set no delay on connections. Theoretically we might at some point want
  // to make this configurable.
  connection_->noDelay(true);
  connection_->addConnectionCallbacks(*this);
  stats_.downstream_cx_total_.inc();
  stats_.downstream_cx_active_.inc();
}

ConnectionHandlerImpl::ActiveConnection::~ActiveConnection() {
  stats_.downstream_cx_active_.dec();
  stats_.downstream_cx_destroy_.inc();
  conn_length_->complete();
}

ListenerStats ConnectionHandlerImpl::generateStats(const std::string& prefix, Stats::Store& store) {
  std::string final_prefix = fmt::format("listener.{}.", prefix);
  return {ALL_LISTENER_STATS(POOL_COUNTER_PREFIX(store, final_prefix),
                             POOL_GAUGE_PREFIX(store, final_prefix),
                             POOL_TIMER_PREFIX(store, final_prefix))};
}

void ConnectionHandlerImpl::startWatchdog() {
  watchdog_timer_ = dispatcher_->createTimer([this]() -> void {
    auto delta = std::chrono::system_clock::now() - last_watchdog_time_;
    if (delta > std::chrono::milliseconds(200)) {
      watchdog_miss_counter_.inc();
    }
    if (delta > std::chrono::milliseconds(1000)) {
      watchdog_mega_miss_counter_.inc();
    }

    last_watchdog_time_ = std::chrono::system_clock::now();
    watchdog_timer_->enableTimer(std::chrono::milliseconds(100));
  });

  last_watchdog_time_ = std::chrono::system_clock::now();
  watchdog_timer_->enableTimer(std::chrono::milliseconds(100));
}

} // Server
