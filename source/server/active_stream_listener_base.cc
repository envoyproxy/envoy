#include "source/server/active_stream_listener_base.h"

#include "envoy/network/filter.h"

namespace Envoy {
namespace Server {

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
    access_log->log(nullptr, nullptr, nullptr, stream_info);
  }
}

void ActiveStreamListenerBase::newConnection(Network::ConnectionSocketPtr&& socket,
                                             std::unique_ptr<StreamInfo::StreamInfo> stream_info) {
  // Find matching filter chain.
  const auto filter_chain = config_->filterChainManager().findFilterChain(*socket);
  if (filter_chain == nullptr) {
    RELEASE_ASSERT(socket->addressProvider().remoteAddress() != nullptr, "");
    ENVOY_LOG(debug, "closing connection from {}: no matching filter chain found",
              socket->addressProvider().remoteAddress()->asString());
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
  auto server_conn_ptr = dispatcher().createServerConnection(
      std::move(socket), std::move(transport_socket), *stream_info);
  if (const auto timeout = filter_chain->transportSocketConnectTimeout();
      timeout != std::chrono::milliseconds::zero()) {
    server_conn_ptr->setTransportSocketConnectTimeout(timeout);
  }
  server_conn_ptr->setBufferLimits(config_->perConnectionBufferLimitBytes());
  RELEASE_ASSERT(server_conn_ptr->addressProvider().remoteAddress() != nullptr, "");
  const bool empty_filter_chain = !config_->filterChainFactory().createNetworkFilterChain(
      *server_conn_ptr, filter_chain->networkFilterFactories());
  if (empty_filter_chain) {
    ENVOY_CONN_LOG(debug, "closing connection from {}: no filters", *server_conn_ptr,
                   server_conn_ptr->addressProvider().remoteAddress()->asString());
    server_conn_ptr->close(Network::ConnectionCloseType::NoFlush);
  }
  newActiveConnection(*filter_chain, std::move(server_conn_ptr), std::move(stream_info));
}

} // namespace Server
} // namespace Envoy
