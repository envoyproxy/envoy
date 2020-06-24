#include "extensions/upstreams/http/tcp/upstream_request.h"

#include <cstdint>
#include <memory>

#include "envoy/upstream/upstream.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/message_impl.h"
#include "common/network/transport_socket_options_impl.h"
#include "common/router/router.h"

#include "extensions/common/proxy_protocol/proxy_protocol_header.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Tcp {

void TcpConnPool::onPoolReady(Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                              Upstream::HostDescriptionConstSharedPtr host) {
  upstream_handle_ = nullptr;
  Network::Connection& latched_conn = conn_data->connection();
  auto upstream =
      std::make_unique<TcpUpstream>(&callbacks_->upstreamToDownstream(), std::move(conn_data));
  callbacks_->onPoolReady(std::move(upstream), host, latched_conn.localAddress(),
                          latched_conn.streamInfo());
}

TcpUpstream::TcpUpstream(Router::UpstreamToDownstream* upstream_request,
                         Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& upstream)
    : upstream_request_(upstream_request), upstream_conn_data_(std::move(upstream)) {
  upstream_conn_data_->connection().enableHalfClose(true);
  upstream_conn_data_->addUpstreamCallbacks(*this);
}

void TcpUpstream::encodeData(Buffer::Instance& data, bool end_stream) {
  upstream_conn_data_->connection().write(data, end_stream);
}

void TcpUpstream::encodeHeaders(const Envoy::Http::RequestHeaderMap&, bool end_stream) {
  // Headers should only happen once, so use this opportunity to add the proxy
  // proto header, if configured.
  ASSERT(upstream_request_->routeEntry().connectConfig().has_value());
  Buffer::OwnedImpl data;
  auto& connect_config = upstream_request_->routeEntry().connectConfig().value();
  if (connect_config.has_proxy_protocol_config()) {
    Extensions::Common::ProxyProtocol::generateProxyProtoHeader(
        connect_config.proxy_protocol_config(), upstream_request_->connection(), data);
  }

  if (data.length() != 0 || end_stream) {
    upstream_conn_data_->connection().write(data, end_stream);
  }

  // TcpUpstream::encodeHeaders is called after the UpstreamRequest is fully initialized. Also use
  // this time to synthesize the 200 response headers downstream to complete the CONNECT handshake.
  Envoy::Http::ResponseHeaderMapPtr headers{
      Envoy::Http::createHeaderMap<Envoy::Http::ResponseHeaderMapImpl>(
          {{Envoy::Http::Headers::get().Status, "200"}})};
  upstream_request_->decodeHeaders(std::move(headers), false);
}

void TcpUpstream::encodeTrailers(const Envoy::Http::RequestTrailerMap&) {
  Buffer::OwnedImpl data;
  upstream_conn_data_->connection().write(data, true);
}

void TcpUpstream::readDisable(bool disable) {
  if (upstream_conn_data_->connection().state() != Network::Connection::State::Open) {
    return;
  }
  upstream_conn_data_->connection().readDisable(disable);
}

void TcpUpstream::resetStream() {
  upstream_request_ = nullptr;
  upstream_conn_data_->connection().close(Network::ConnectionCloseType::NoFlush);
}

void TcpUpstream::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  upstream_request_->decodeData(data, end_stream);
}

void TcpUpstream::onEvent(Network::ConnectionEvent event) {
  if (event != Network::ConnectionEvent::Connected && upstream_request_) {
    upstream_request_->onResetStream(Envoy::Http::StreamResetReason::ConnectionTermination, "");
  }
}

void TcpUpstream::onAboveWriteBufferHighWatermark() {
  if (upstream_request_) {
    upstream_request_->onAboveWriteBufferHighWatermark();
  }
}

void TcpUpstream::onBelowWriteBufferLowWatermark() {
  if (upstream_request_) {
    upstream_request_->onBelowWriteBufferLowWatermark();
  }
}

} // namespace Tcp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
