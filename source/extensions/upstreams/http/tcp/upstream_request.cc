#include "source/extensions/upstreams/http/tcp/upstream_request.h"

#include <cstdint>
#include <memory>

#include "envoy/upstream/upstream.h"

#include "source/common/common/assert.h"
#include "source/common/common/utility.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/router/router.h"
#include "source/extensions/common/proxy_protocol/proxy_protocol_header.h"

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
  callbacks_->onPoolReady(std::move(upstream), host, latched_conn.connectionInfoProvider(),
                          latched_conn.streamInfo(), {});
}

TcpUpstream::TcpUpstream(Router::UpstreamToDownstream* upstream_request,
                         Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& upstream)
    : upstream_request_(upstream_request), upstream_conn_data_(std::move(upstream)),
      force_reset_on_upstream_half_close_(Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.allow_multiplexed_upstream_half_close")) {
  upstream_conn_data_->connection().enableHalfClose(true);
  upstream_conn_data_->addUpstreamCallbacks(*this);
}

void TcpUpstream::encodeData(Buffer::Instance& data, bool end_stream) {
  downstream_complete_ = end_stream;
  bytes_meter_->addWireBytesSent(data.length());
  upstream_conn_data_->connection().write(data, end_stream);
}

Envoy::Http::Status TcpUpstream::encodeHeaders(const Envoy::Http::RequestHeaderMap&,
                                               bool end_stream) {
  downstream_complete_ = end_stream;
  // Headers should only happen once, so use this opportunity to add the proxy
  // proto header, if configured.
  const Router::RouteEntry* route_entry = upstream_request_->route().routeEntry();
  ASSERT(route_entry != nullptr);
  if (route_entry->connectConfig().has_value()) {
    Buffer::OwnedImpl data;
    const auto& connect_config = route_entry->connectConfig();
    if (connect_config->has_proxy_protocol_config() &&
        upstream_request_->connection().has_value()) {
      Extensions::Common::ProxyProtocol::generateProxyProtoHeader(
          connect_config->proxy_protocol_config(), *upstream_request_->connection(), data);
    }

    if (data.length() != 0 || end_stream) {
      // Count header bytes for proxy proto.
      bytes_meter_->addHeaderBytesSent(data.length());
      bytes_meter_->addWireBytesSent(data.length());
      upstream_conn_data_->connection().write(data, end_stream);
    }
  }

  // TcpUpstream::encodeHeaders is called after the UpstreamRequest is fully initialized. Also use
  // this time to synthesize the 200 response headers downstream to complete the CONNECT handshake.
  Envoy::Http::ResponseHeaderMapPtr headers{
      Envoy::Http::createHeaderMap<Envoy::Http::ResponseHeaderMapImpl>(
          {{Envoy::Http::Headers::get().Status, "200"}})};
  upstream_request_->decodeHeaders(std::move(headers), /*end_stream=*/false);
  return Envoy::Http::okStatus();
}

void TcpUpstream::encodeTrailers(const Envoy::Http::RequestTrailerMap&) {
  downstream_complete_ = true;
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
  upstream_conn_data_->connection().close(Network::ConnectionCloseType::NoFlush,
                                          "tcp_upstream_reset_stream");
}

void TcpUpstream::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  // In the TCP proxy case the filter manager used to trigger the full stream closure when the
  // upstream server half closed its end of the TCP connection. With the
  // allow_multiplexed_upstream_half_close enabled filter manager no longer closes stream that were
  // half closed by upstream before downstream. To keep the behavior the same for TCP proxy the
  // upstream force closes the connection when server half closes.
  //
  // Save the indicator to close the stream before calling the decodeData since when the
  // allow_multiplexed_upstream_half_close is false the call to decodeHeader with end_stream==true
  // will delete the TcpUpstream object.
  // NOTE: it this point Envoy can not support half closed TCP upstream as there is currently no
  // distinction between half closed vs fully closed TCP peers.
  const bool force_reset =
      force_reset_on_upstream_half_close_ && end_stream && !downstream_complete_;
  bytes_meter_->addWireBytesReceived(data.length());
  upstream_request_->decodeData(data, end_stream);
  // force_reset is true only when allow_multiplexed_upstream_half_close is true and in this case
  // the decodeData will never cause the stream to be closed and as such it safe to access
  // upstream_request_
  if (force_reset && upstream_request_) {
    upstream_request_->onResetStream(Envoy::Http::StreamResetReason::ConnectionTermination,
                                     "half_close_initiated_full_close");
  }
}

void TcpUpstream::onEvent(Network::ConnectionEvent event) {
  if ((event == Network::ConnectionEvent::LocalClose ||
       event == Network::ConnectionEvent::RemoteClose) &&
      upstream_request_) {
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
