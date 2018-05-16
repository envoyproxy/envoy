#include "common/http/websocket/ws_handler_impl.h"

#include "envoy/network/connection.h"

#include "common/common/enum_to_int.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/http1/codec_impl.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Http {
namespace WebSocket {

TcpProxy::ConfigSharedPtr Config(const envoy::api::v2::route::RouteAction& route_config,
                                 Server::Configuration::FactoryContext& factory_context) {
  envoy::config::filter::network::tcp_proxy::v2::TcpProxy tcp_config;

  // Set the default value. This may be overwritten below.
  tcp_config.set_stat_prefix("websocket");

  if (route_config.has_websocket_config()) {
    // WebSocket has its own TcpProxy config type because some of the fields
    // in envoy::config::filter::network::tcp_proxy::v2::TcpProxy don't apply, and some
    // are duplicated in the route config (such as the upstream cluster).
    const envoy::api::v2::route::RouteAction::WebSocketProxyConfig& ws_config =
        route_config.websocket_config();

    if (!ws_config.stat_prefix().empty()) {
      tcp_config.set_stat_prefix(ws_config.stat_prefix());
    }

    if (ws_config.has_idle_timeout()) {
      *tcp_config.mutable_idle_timeout() = ws_config.idle_timeout();
    }

    if (ws_config.has_max_connect_attempts()) {
      *tcp_config.mutable_max_connect_attempts() = ws_config.max_connect_attempts();
    }
  }
  return std::make_shared<TcpProxy::Config>(tcp_config, factory_context);
}

WsHandlerImpl::WsHandlerImpl(HeaderMap& request_headers,
                             const RequestInfo::RequestInfo& request_info,
                             const Router::RouteEntry& route_entry,
                             WebSocketProxyCallbacks& callbacks,
                             Upstream::ClusterManager& cluster_manager,
                             Network::ReadFilterCallbacks* read_callbacks,
                             TcpProxy::ConfigSharedPtr config)
    : TcpProxy::Filter(config, cluster_manager), request_headers_(request_headers),
      request_info_(request_info), route_entry_(route_entry), ws_callbacks_(callbacks) {

  // set_connection_stats == false because the http connection manager has already set them
  // and they will be inaccurate if we change them now.
  initialize(*read_callbacks, false);
  onNewConnection();
}

void WsHandlerImpl::onInitFailure(UpstreamFailureReason failure_reason) {
  ASSERT(state_ == ConnectState::PreConnect);
  state_ = ConnectState::Failed;

  Code http_code = Code::InternalServerError;
  switch (failure_reason) {
  case UpstreamFailureReason::CONNECT_FAILED:
    http_code = Code::GatewayTimeout;
    break;
  case UpstreamFailureReason::NO_HEALTHY_UPSTREAM:
  case UpstreamFailureReason::RESOURCE_LIMIT_EXCEEDED:
  case UpstreamFailureReason::NO_ROUTE:
    http_code = Code::ServiceUnavailable;
    break;
  }

  HeaderMapImpl headers{{Headers::get().Status, std::to_string(enumToInt(http_code))}};
  ws_callbacks_.sendHeadersOnlyResponse(headers);
}

Network::FilterStatus WsHandlerImpl::onData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(trace, "WsHandlerImpl::onData with buffer length {}, end_stream == {}", data.length(),
            end_stream);

  switch (state_) {
  case ConnectState::PreConnect:
    // It is expected that TcpProxy will readDisable(true) the downstream connection until it is
    // ready to send data to the upstream connection, so onData() should be called zero or one times
    // before state_ becomes Connected.
    ASSERT(queued_data_.length() == 0);

    ENVOY_LOG(trace, "WsHandlerImpl::onData is NOT connected");
    queued_data_.move(data);
    queued_end_stream_ = end_stream;
    break;
  case ConnectState::Connected:
    // Data should have been drained already when we transitioned to Connected.
    ASSERT(queued_data_.length() == 0);

    ENVOY_LOG(trace, "WsHandlerImpl::onData is connected");
    return TcpProxy::Filter::onData(data, end_stream);
  case ConnectState::Failed:
    ENVOY_LOG(trace, "WsHandlerImpl::onData state_ == Failed; discarding");
    break;
  }

  return Network::FilterStatus::StopIteration;
}

void WsHandlerImpl::onConnectionSuccess() {
  // path and host rewrites
  route_entry_.finalizeRequestHeaders(request_headers_, request_info_);
  // for auto host rewrite
  if (route_entry_.autoHostRewrite() && !read_callbacks_->upstreamHost()->hostname().empty()) {
    request_headers_.Host()->value(read_callbacks_->upstreamHost()->hostname());
  }

  // Wrap upstream connection in HTTP Connection, so that we can
  // re-use the HTTP1 codec to send upgrade headers to upstream
  // host.

  // TODO (rshriram): This is a not the strictest WebSocket
  // implementation, as we do not really check the response headers
  // to ensure that the upstream really accepted the upgrade
  // request. Doing so requires re-doing bunch of HTTP/1.1 req-resp
  // pair stuff and that is going to just complicate this code. The
  // client could technically send a body along with the request.
  // The server could send a body along with the upgrade response,
  // or even send a redirect. A simpler way to handle all of this would be
  // to expose the TCP connection associated with the response object, in encodeHeaders.
  // Once we see a 101 switching protocols, we could remove the connection from
  // the connection pool. The current approach is a stop gap solution, where
  // we put the onus on the user to tell us if a route (and corresponding upstream)
  // is supposed to allow websocket upgrades or not.
  Http1::ClientConnectionImpl upstream_http(*upstream_connection_, http_conn_callbacks_);
  Http1::RequestStreamEncoderImpl upstream_request = Http1::RequestStreamEncoderImpl(upstream_http);
  upstream_request.encodeHeaders(request_headers_, false);
  ASSERT(state_ == ConnectState::PreConnect);
  state_ = ConnectState::Connected;
  if (queued_data_.length() > 0 || queued_end_stream_) {
    ENVOY_LOG(trace, "WsHandlerImpl::onConnectionSuccess calling TcpProxy::onData");
    TcpProxy::Filter::onData(queued_data_, queued_end_stream_);
    ASSERT(queued_data_.length() == 0);
  }
}

} // namespace WebSocket
} // namespace Http
} // namespace Envoy
