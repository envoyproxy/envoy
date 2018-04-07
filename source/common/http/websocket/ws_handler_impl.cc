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

WsHandlerImpl::WsHandlerImpl(HeaderMap& request_headers,
                             const RequestInfo::RequestInfo& request_info,
                             const Router::RouteEntry& route_entry, WsHandlerCallbacks& callbacks,
                             Upstream::ClusterManager& cluster_manager,
                             Network::ReadFilterCallbacks* read_callbacks)
    : Extensions::NetworkFilters::TcpProxy::TcpProxyFilter(nullptr, cluster_manager),
      request_headers_(request_headers), request_info_(request_info), route_entry_(route_entry),
      ws_callbacks_(callbacks) {

  initializeReadFilterCallbacks(*read_callbacks);
}

void WsHandlerImpl::onInitFailure(UpstreamFailureReason failure_reason) {
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
}

} // namespace WebSocket
} // namespace Http
} // namespace Envoy
