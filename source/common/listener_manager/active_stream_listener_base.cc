#include "source/common/listener_manager/active_stream_listener_base.h"

#include "envoy/network/filter.h"

#include "source/common/stats/timespan_impl.h"

namespace Envoy {
namespace Server {

class FilterChainInfoImpl : public Network::FilterChainInfo {
public:
  FilterChainInfoImpl(absl::string_view name) : name_(name) {}

  // Network::FilterChainInfo
  absl::string_view name() const override { return name_; }

private:
  const std::string name_;
};

ActiveStreamListenerBase::ActiveStreamListenerBase(Network::ConnectionHandler& parent,
                                                   Event::Dispatcher& dispatcher,
                                                   Network::ListenerPtr&& listener,
                                                   Network::ListenerConfig& config)
    : ActiveListenerImplBase(parent, &config), parent_(parent),
      listener_filters_timeout_(config.listenerFiltersTimeout()),
      continue_on_listener_filters_timeout_(config.continueOnListenerFiltersTimeout()),
      listener_(std::move(listener)), dispatcher_(dispatcher) {}

void ActiveStreamListenerBase::emitLogs(Network::ListenerConfig& config,
                                        StreamInfo::StreamInfo& stream_info) {
  stream_info.onRequestComplete();
  for (const auto& access_log : config.accessLogs()) {
    access_log->log({}, stream_info);
  }
}

void ActiveStreamListenerBase::newConnection(Network::ConnectionSocketPtr&& socket,
                                             std::unique_ptr<StreamInfo::StreamInfo> stream_info) {
  // Find matching filter chain.
  const auto filter_chain = config_->filterChainManager().findFilterChain(*socket, *stream_info);
  if (filter_chain == nullptr) {
    RELEASE_ASSERT(socket->connectionInfoProvider().remoteAddress() != nullptr, "");
    ENVOY_LOG(debug, "closing connection from {}: no matching filter chain found",
              socket->connectionInfoProvider().remoteAddress()->asString());
    stats_.no_filter_chain_match_.inc();
    stream_info->setResponseFlag(StreamInfo::ResponseFlag::NoRouteFound);
    stream_info->setResponseCodeDetails(StreamInfo::ResponseCodeDetails::get().FilterChainNotFound);
    emitLogs(*config_, *stream_info);
    socket->close();
    return;
  }

  socket->connectionInfoProvider().setFilterChainInfo(
      std::make_shared<FilterChainInfoImpl>(filter_chain->name()));

  auto transport_socket = filter_chain->transportSocketFactory().createDownstreamTransportSocket();
  auto server_conn_ptr = dispatcher().createServerConnection(
      std::move(socket), std::move(transport_socket), *stream_info);
  if (const auto timeout = filter_chain->transportSocketConnectTimeout();
      timeout != std::chrono::milliseconds::zero()) {
    server_conn_ptr->setTransportSocketConnectTimeout(
        timeout, stats_.downstream_cx_transport_socket_connect_timeout_);
  }
  server_conn_ptr->setBufferLimits(config_->perConnectionBufferLimitBytes());
  RELEASE_ASSERT(server_conn_ptr->connectionInfoProvider().remoteAddress() != nullptr, "");
  const bool empty_filter_chain = !config_->filterChainFactory().createNetworkFilterChain(
      *server_conn_ptr, filter_chain->networkFilterFactories());
  if (empty_filter_chain) {
    ENVOY_CONN_LOG(debug, "closing connection from {}: no filters", *server_conn_ptr,
                   server_conn_ptr->connectionInfoProvider().remoteAddress()->asString());
    server_conn_ptr->close(Network::ConnectionCloseType::NoFlush, "no_filters");
  }
  newActiveConnection(*filter_chain, std::move(server_conn_ptr), std::move(stream_info));
}

ActiveConnections::ActiveConnections(OwnedActiveStreamListenerBase& listener,
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

  // Active connections on the handler (not listener). The per listener connections have already
  // been incremented at this point either via the connection balancer or in the socket accept
  // path if there is no configured balancer.
  listener.parent_.incNumConnections();
}

ActiveTcpConnection::~ActiveTcpConnection() {
  ActiveStreamListenerBase::emitLogs(*active_connections_.listener_.config_, *stream_info_);
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

void ActiveTcpConnection::onEvent(Network::ConnectionEvent event) {
  ENVOY_CONN_LOG(trace, "tcp connection on event {}", *connection_, static_cast<int>(event));
  // Any event leads to destruction of the connection.
  if (event == Network::ConnectionEvent::LocalClose ||
      event == Network::ConnectionEvent::RemoteClose) {
    stream_info_->setDownstreamTransportFailureReason(connection_->transportFailureReason());
    active_connections_.listener_.removeConnection(*this);
  }
}

void OwnedActiveStreamListenerBase::removeConnection(ActiveTcpConnection& connection) {
  ENVOY_CONN_LOG(debug, "adding to cleanup list", *connection.connection_);
  ActiveConnections& active_connections = connection.active_connections_;
  ActiveConnectionPtr removed = connection.removeFromList(active_connections.connections_);
  dispatcher().deferredDelete(std::move(removed));
  // Delete map entry if and only if connections_ becomes empty.
  if (active_connections.connections_.empty()) {
    auto iter = connections_by_context_.find(&active_connections.filter_chain_);
    ASSERT(iter != connections_by_context_.end());
    // To cover the lifetime of every single connection, Connections need to be deferred deleted
    // because the previously contained connection is deferred deleted.
    dispatcher().deferredDelete(std::move(iter->second));
    // The erase will break the iteration over the connections_by_context_ during the deletion.
    if (!is_deleting_) {
      connections_by_context_.erase(iter);
    }
  }
}

ActiveConnections& OwnedActiveStreamListenerBase::getOrCreateActiveConnections(
    const Network::FilterChain& filter_chain) {
  ActiveConnectionCollectionPtr& connections = connections_by_context_[&filter_chain];
  if (connections == nullptr) {
    connections = std::make_unique<ActiveConnections>(*this, filter_chain);
  }
  return *connections;
}

void OwnedActiveStreamListenerBase::removeFilterChain(const Network::FilterChain* filter_chain) {
  auto iter = connections_by_context_.find(filter_chain);
  if (iter == connections_by_context_.end()) {
    // It is possible when listener is stopping.
  } else {
    auto& connections = iter->second->connections_;
    while (!connections.empty()) {
      connections.front()->connection_->close(Network::ConnectionCloseType::NoFlush,
                                              "filter_chain_is_being_removed");
    }
    // Since is_deleting_ is on, we need to manually remove the map value and drive the
    // iterator. Defer delete connection container to avoid race condition in destroying
    // connection.
    dispatcher().deferredDelete(std::move(iter->second));
    connections_by_context_.erase(iter);
  }
}

} // namespace Server
} // namespace Envoy
