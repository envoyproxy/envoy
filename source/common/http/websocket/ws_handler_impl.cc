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
    : cluster_name_(cluster_name), request_headers_(request_headers), route_entry_(route_entry),
      stream_(stream), cluster_manager_(cluster_manager), downstream_callbacks_(*this),
      upstream_callbacks_(new UpstreamCallbacks(*this)) {}

WsHandlerImpl::~WsHandlerImpl() {
  if (upstream_connection_) {
    read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_destroy_.inc();
    read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_active_.dec();
    read_callbacks_->upstreamHost()->stats().cx_active_.dec();
    read_callbacks_->upstreamHost()
        ->cluster()
        .resourceManager(Upstream::ResourcePriority::Default)
        .connections()
        .dec();
    connected_timespan_->complete();
  }
}

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
  cluster->resourceManager(Upstream::ResourcePriority::Default).connections().inc();

  upstream_connection_->addReadFilter(upstream_callbacks_);
  upstream_connection_->addConnectionCallbacks(*upstream_callbacks_);
  upstream_connection_->setBufferStats(
      {read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_rx_bytes_total_,
       read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_rx_bytes_buffered_,
       read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_tx_bytes_total_,
       read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_tx_bytes_buffered_});
  upstream_connection_->connect();
  upstream_connection_->noDelay(true);

  connect_timeout_timer_ = read_callbacks_->connection().dispatcher().createTimer(
      [this]() -> void { onConnectTimeout(); });
  connect_timeout_timer_->enableTimer(cluster->connectTimeout());

  read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_total_.inc();
  read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_active_.inc();
  read_callbacks_->upstreamHost()->stats().cx_total_.inc();
  read_callbacks_->upstreamHost()->stats().cx_active_.inc();
  connect_timespan_ =
      read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_connect_ms_.allocateSpan();
  connected_timespan_ =
      read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_length_ms_.allocateSpan();
  return;
}

void WsHandlerImpl::onConnectTimeout() {
  read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_connect_timeout_.inc();
  upstream_connection_.get()->close(Network::ConnectionCloseType::NoFlush);
  Http::HeaderMapPtr headers = std::unique_ptr<Http::HeaderMap>(new Http::HeaderMapImpl());
  headers->addStatic(Headers::get().Status, std::to_string(enumToInt(Http::Code::GatewayTimeout)));
  stream_.encodeHeaders(std::move(headers), true);
}

Network::FilterStatus WsHandlerImpl::onData(Buffer::Instance& data) {
  upstream_connection_->write(data);
  ASSERT(0 == data.length());
  return Network::FilterStatus::StopIteration;
}

void WsHandlerImpl::onDownstreamEvent(uint32_t event) {
  if ((event & Network::ConnectionEvent::RemoteClose ||
       event & Network::ConnectionEvent::LocalClose) &&
      upstream_connection_) {
    // TODO(mattklein123): If we close without flushing here we may drop some data. The downstream
    // connection is about to go away. So to support this we need to either have a way for the
    // downstream connection to stick around, or, we need to be able to pass this connection to a
    // flush worker which will attempt to flush the remaining data with a timeout.
    upstream_connection_->close(Network::ConnectionCloseType::NoFlush);
  }
}

void WsHandlerImpl::onUpstreamData(Buffer::Instance& data) {
  read_callbacks_->connection().write(data);
  ASSERT(0 == data.length());
}

void WsHandlerImpl::onUpstreamEvent(uint32_t event) {
  if (event & Network::ConnectionEvent::RemoteClose) {
    read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_destroy_remote_.inc();
  }

  if (event & Network::ConnectionEvent::LocalClose) {
    read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_destroy_local_.inc();
  }

  if (event & Network::ConnectionEvent::RemoteClose) {
    if (connect_timeout_timer_) {
      read_callbacks_->upstreamHost()->cluster().stats().upstream_cx_connect_fail_.inc();
      read_callbacks_->upstreamHost()->stats().cx_connect_fail_.inc();
    }

    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
  } else if (event & Network::ConnectionEvent::Connected) {
    connect_timespan_->complete();
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
    // the connection pool
    Http::Http1::ClientConnectionImpl upstream_http(*upstream_connection_, *upstream_callbacks_);
    Http::Http1::RequestStreamEncoderImpl upstream_request =
        Http1::RequestStreamEncoderImpl(upstream_http);
    upstream_request.encodeHeaders(request_headers_, false);
  }

  if (connect_timeout_timer_) {
    connect_timeout_timer_->disableTimer();
    connect_timeout_timer_.reset();
  }
}

} // namespace WebSocket
} // namespace Http
} // namespace Envoy
