#include "common/http/websocket/ws_handler_impl.h"

#include "envoy/buffer/buffer.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "envoy/router/router.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/common/utility.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/http1/codec_impl.h"
#include "common/http/utility.h"
#include "common/network/utility.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Http {
namespace WebSocket {

WsHandlerImpl::WsHandlerImpl(const std::string& cluster_name, Http::HeaderMap& request_headers,
                             const Router::RouteEntry* route_entry,
                             StreamDecoderFilterCallbacks& stream,
                             Upstream::ClusterManager& cluster_manager)
    : Filter::TcpProxy(nullptr, cluster_manager), cluster_name_(cluster_name),
      request_headers_(request_headers), route_entry_(route_entry), stream_(stream) {}

WsHandlerImpl::~WsHandlerImpl() {}

void WsHandlerImpl::initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  read_callbacks_ = &callbacks;
  read_callbacks_->connection().addConnectionCallbacks(downstream_callbacks_);
  Upstream::ThreadLocalCluster* thread_local_cluster = cluster_manager_.get(cluster_name_);

  if (thread_local_cluster) {
    ENVOY_CONN_LOG(debug, "creating connection to upstream cluster {}",
                   read_callbacks_->connection(), cluster_name_);
  } else {
    Http::HeaderMapPtr headers = std::unique_ptr<Http::HeaderMap>(new Http::HeaderMapImpl());
    headers->addStatic(Headers::get().Status, std::to_string(enumToInt(Http::Code::NotFound)));
    stream_.encodeHeaders(std::move(headers), true);
    return;
  }

  Upstream::ClusterInfoConstSharedPtr cluster = thread_local_cluster->info();

  if (!cluster->resourceManager(Upstream::ResourcePriority::Default).connections().canCreate()) {
    cluster->stats().upstream_cx_overflow_.inc();
    Http::HeaderMapPtr headers = std::unique_ptr<Http::HeaderMap>(new Http::HeaderMapImpl());
    headers->addStatic(Headers::get().Status,
                       std::to_string(enumToInt(Http::Code::ServiceUnavailable)));
    stream_.encodeHeaders(std::move(headers), true);
    return;
  }

  Upstream::Host::CreateConnectionData conn_info =
      cluster_manager_.tcpConnForCluster(cluster_name_);

  // path and host rewrites
  route_entry_->finalizeRequestHeaders(request_headers_);
  // for auto host rewrite
  if (route_entry_->autoHostRewrite() && !conn_info.host_description_->hostname().empty()) {
    request_headers_.Host()->value(conn_info.host_description_->hostname());
  }

  upstream_connection_ = std::move(conn_info.connection_);
  read_callbacks_->upstreamHost(conn_info.host_description_);
  if (!upstream_connection_) {
    Http::HeaderMapPtr headers = std::unique_ptr<Http::HeaderMap>(new Http::HeaderMapImpl());
    headers->addStatic(Headers::get().Status,
                       std::to_string(enumToInt(Http::Code::ServiceUnavailable)));
    stream_.encodeHeaders(std::move(headers), true);
    return;
  }

  Filter::TcpProxy::commonInitializeUpstreamConnection(cluster);
}

void WsHandlerImpl::onConnectTimeout() {
  ENVOY_CONN_LOG(debug, "connect timeout", read_callbacks_->connection());
  read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_connect_timeout_.inc();
  upstream_connection_.get()->close(Network::ConnectionCloseType::NoFlush);
  Http::HeaderMapPtr headers = std::unique_ptr<Http::HeaderMap>(new Http::HeaderMapImpl());
  headers->addStatic(Headers::get().Status, std::to_string(enumToInt(Http::Code::GatewayTimeout)));
  stream_.encodeHeaders(std::move(headers), true);
}

void WsHandlerImpl::onUpstreamEvent(uint32_t event) {
  Filter::TcpProxy::onUpstreamEvent(event);

  if (!(event & Network::ConnectionEvent::RemoteClose) &&
      (event & Network::ConnectionEvent::Connected)) {
    // Wrap upstream connection in HTTP Connection, so that we can
    // re-use the HTTP1 codec to send upgrade headers to upstream
    // host.

    // TODO (rshriram): This is a not the strictest WebSocket
    // implementation, as we do not really check the response headers
    // to ensure that the upstream really accepted the upgrade
    // request.  Doing so requires re-doing bunch of HTTP/1.1 req-resp
    // pair stuff and that is going to just complicate this code. The
    // client could technically send a body along with the request.
    // The server could send a body along with the upgrade response,
    // or even send a redirect. A simpler way to handle all of this would be
    // to expose the TCP connection associated with the response object, in encodeHeaders.
    // Once we see a 101 switching protocols, we could remove the connection from
    // the connection pool. The current approach is a stop gap solution, where
    // we put the onus on the user to tell us if a route (and corresponding upstream)
    // is supposed to allow websocket upgrades or not.
    Http::Http1::ClientConnectionImpl upstream_http(*upstream_connection_, http_conn_callbacks_);
    Http::Http1::RequestStreamEncoderImpl upstream_request =
        Http1::RequestStreamEncoderImpl(upstream_http);
    upstream_request.encodeHeaders(request_headers_, false);
  }
}

} // namespace WebSocket
} // namespace Http
} // namespace Envoy
