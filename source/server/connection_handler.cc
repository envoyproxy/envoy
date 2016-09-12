#include "connection_handler.h"

#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/network/filter.h"

#include "common/api/api_impl.h"

ConnectionHandler::ConnectionHandler(Stats::Store& stats_store, spdlog::logger& logger,
                                     std::chrono::milliseconds flush_interval_msec)
    : stats_store_(stats_store), logger_(logger), api_(new Api::Impl(flush_interval_msec)),
      dispatcher_(api_->allocateDispatcher()),
      watchdog_miss_counter_(stats_store.counter("server.watchdog_miss")),
      watchdog_mega_miss_counter_(stats_store.counter("server.watchdog_mega_miss")) {}

ConnectionHandler::~ConnectionHandler() { closeConnections(); }

void ConnectionHandler::addListener(Network::FilterChainFactory& factory,
                                    Network::ListenSocket& socket, bool use_proxy_proto) {
  listeners_.emplace_back(new ActiveListener(*this, socket, factory, use_proxy_proto));
}

void ConnectionHandler::addSslListener(Network::FilterChainFactory& factory,
                                       Ssl::ServerContext& ssl_ctx, Network::ListenSocket& socket,
                                       bool use_proxy_proto) {
  listeners_.emplace_back(new SslActiveListener(*this, ssl_ctx, socket, factory, use_proxy_proto));
}

void ConnectionHandler::closeConnections() {
  while (!connections_.empty()) {
    connections_.front()->connection_->close(Network::ConnectionCloseType::NoFlush);
  }

  dispatcher_->clearDeferredDeleteList();
}

void ConnectionHandler::closeListeners() {
  for (ActiveListenerPtr& l : listeners_) {
    l->listener_.reset();
  }
}

void ConnectionHandler::removeConnection(ActiveConnection& connection) {
  conn_log(logger_, info, "adding to cleanup list", *connection.connection_);
  ActiveConnectionPtr removed = connection.removeFromList(connections_);
  dispatcher_->deferredDelete(std::move(removed));
  num_connections_--;
}

ConnectionHandler::ActiveListener::ActiveListener(ConnectionHandler& parent,
                                                  Network::ListenSocket& socket,
                                                  Network::FilterChainFactory& factory,
                                                  bool use_proxy_proto)
    : ActiveListener(parent, parent.dispatcher_->createListener(socket, *this, parent.stats_store_,
                                                                use_proxy_proto),
                     factory, socket.name()) {}

ConnectionHandler::ActiveListener::ActiveListener(ConnectionHandler& parent,
                                                  Network::ListenerPtr&& listener,
                                                  Network::FilterChainFactory& factory,
                                                  const std::string& stats_prefix)
    : parent_(parent), factory_(factory), stats_(generateStats(stats_prefix, parent.stats_store_)) {
  listener_ = std::move(listener);
}

ConnectionHandler::SslActiveListener::SslActiveListener(ConnectionHandler& parent,
                                                        Ssl::ServerContext& ssl_ctx,
                                                        Network::ListenSocket& socket,
                                                        Network::FilterChainFactory& factory,
                                                        bool use_proxy_proto)
    : ActiveListener(parent, parent.dispatcher_->createSslListener(
                                 ssl_ctx, socket, *this, parent.stats_store_, use_proxy_proto),
                     factory, socket.name()) {}

void ConnectionHandler::ActiveListener::onNewConnection(Network::ConnectionPtr&& new_connection) {
  conn_log(parent_.logger_, info, "new connection", *new_connection);
  factory_.createFilterChain(*new_connection);
  ActiveConnectionPtr active_connection(
      new ActiveConnection(parent_, std::move(new_connection), stats_));
  active_connection->moveIntoList(std::move(active_connection), parent_.connections_);
  parent_.num_connections_++;
}

ConnectionHandler::ActiveConnection::ActiveConnection(ConnectionHandler& parent,
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

ConnectionHandler::ActiveConnection::~ActiveConnection() {
  stats_.downstream_cx_active_.dec();
  stats_.downstream_cx_destroy_.inc();
  conn_length_->complete();
}

ListenerStats ConnectionHandler::generateStats(const std::string& prefix, Stats::Store& store) {
  std::string final_prefix = fmt::format("listener.{}.", prefix);
  return {ALL_LISTENER_STATS(POOL_COUNTER_PREFIX(store, final_prefix),
                             POOL_GAUGE_PREFIX(store, final_prefix),
                             POOL_TIMER_PREFIX(store, final_prefix))};
}

void ConnectionHandler::startWatchdog() {
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
