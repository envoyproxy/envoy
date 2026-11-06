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
#include "source/common/runtime/runtime_features.h"
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
    : upstream_request_(upstream_request), upstream_conn_data_(std::move(upstream)) {
  upstream_conn_data_->connection().enableHalfClose(true);
  upstream_conn_data_->addUpstreamCallbacks(*this);
}

void TcpUpstream::encodeData(Buffer::Instance& data, bool end_stream) {
  bytes_meter_->addWireBytesSent(data.length());
  upstream_conn_data_->connection().write(data, end_stream);
}

Envoy::Http::Status TcpUpstream::encodeHeaders(const Envoy::Http::RequestHeaderMap&,
                                               bool end_stream) {
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
  downstream_headers_sent_ = true;
  upstream_request_->decodeHeaders(std::move(headers), /*end_stream=*/false);
  return Envoy::Http::okStatus();
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
  upstream_conn_data_->connection().close(Network::ConnectionCloseType::NoFlush,
                                          "tcp_upstream_reset_stream");
}

void TcpUpstream::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  bytes_meter_->addWireBytesReceived(data.length());
  if (end_stream && downstream_headers_sent_) {
    saw_end_stream_ = true;
  }
  // Without independent half close, this may destroy this object.
  upstream_request_->decodeData(data, end_stream);
}

void TcpUpstream::onEvent(Network::ConnectionEvent event) {
  if ((event == Network::ConnectionEvent::LocalClose ||
       event == Network::ConnectionEvent::RemoteClose) &&
      upstream_request_) {
    // If the upstream already delivered end_stream and then closed cleanly (e.g. a user-space
    // socket peer finishing a graceful close while the downstream request half is still open), end
    // the downstream stream with 'NO_ERROR' rather than recording 'UpstreamRemoteReset (UC)' / 503.
    const bool no_error_reset =
        saw_end_stream_ &&
        upstream_conn_data_->connection().detectedCloseType() ==
            StreamInfo::DetectedCloseType::Normal &&
        Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.tcp_upstream_no_error_reset_after_end_stream");
    upstream_request_->onResetStream(no_error_reset
                                         ? Envoy::Http::StreamResetReason::RemoteResetNoError
                                         : Envoy::Http::StreamResetReason::ConnectionTermination,
                                     "");
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
