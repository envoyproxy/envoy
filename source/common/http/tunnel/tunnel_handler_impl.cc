#include "common/http/tunnel/tunnel_handler_impl.h"

#include "envoy/network/connection.h"

#include "common/common/enum_to_int.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/http1/codec_impl.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Http {
namespace Tunnel {

TcpProxy::ConfigSharedPtr Config(const envoy::api::v2::route::RouteAction&,
                                 Server::Configuration::FactoryContext& factory_context) {
  envoy::config::filter::network::tcp_proxy::v2::TcpProxy tcp_config;
  // Set the default value. This may be overwritten below.
  tcp_config.set_stat_prefix("tunnel");

  return std::make_shared<TcpProxy::Config>(tcp_config, factory_context);
}

TunnelHandlerImpl::TunnelHandlerImpl(
    HeaderMap& request_headers, StreamInfo::StreamInfo& stream_info,
    const Router::RouteEntry& route_entry, HeadersOnlyCallback& callbacks,
    Upstream::ClusterManager& cluster_manager, Network::ReadFilterCallbacks* read_callbacks,
    TcpProxy::ConfigSharedPtr config, Event::TimeSystem& time_system)
    : TcpProxy::Filter(config, cluster_manager, time_system), request_headers_(request_headers),
      stream_info_(stream_info), route_entry_(route_entry), tunnel_callbacks_(callbacks) {

  // set_connection_stats == false because the http connection manager has already set them
  // and they will be inaccurate if we change them now.
  initialize(*read_callbacks, false);
  onNewConnection();
}

void TunnelHandlerImpl::onInitFailure(UpstreamFailureReason failure_reason) {
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
  tunnel_callbacks_.sendHeadersOnlyResponse(headers);
}

Network::FilterStatus TunnelHandlerImpl::onData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(trace, "TunnelHandlerImpl::onData with buffer length {}, end_stream == {}",
            data.length(), end_stream);

  switch (state_) {
  case ConnectState::PreConnect:
    // It is expected that TcpProxy will readDisable(true) the downstream connection until it is
    // ready to send data to the upstream connection, so onData() should be called zero or one times
    // before state_ becomes Connected.
    ASSERT(queued_data_.length() == 0);

    ENVOY_LOG(trace, "TunnelHandlerImpl::onData is NOT connected");
    queued_data_.move(data);
    queued_end_stream_ = end_stream;
    break;
  case ConnectState::Connected:
    // Data should have been drained already when we transitioned to Connected.
    ASSERT(queued_data_.length() == 0);

    ENVOY_LOG(trace, "TunnelHandlerImpl::onData is connected");
    return TcpProxy::Filter::onData(data, end_stream);
  case ConnectState::Failed:
    ENVOY_LOG(trace, "TunnelHandlerImpl::onData state_ == Failed; discarding");
    break;
  }

  return Network::FilterStatus::StopIteration;
}

void TunnelHandlerImpl::onConnectionSuccess() {
  ENVOY_LOG(trace, "TunnelHandlerImpl:: TCP connection established");
  // path and host rewrites
  bool forward_connect =
      !read_callbacks_->upstreamHost()->hostname().empty() ||
      request_headers_.Host()->value() != read_callbacks_->upstreamHost()->hostname().c_str();

  // for auto host rewrite
  if (forward_connect && route_entry_.autoHostRewrite() &&
      !read_callbacks_->upstreamHost()->hostname().empty()) {
    route_entry_.finalizeRequestHeaders(request_headers_, stream_info_, true);
    request_headers_.Host()->value(read_callbacks_->upstreamHost()->hostname());
  }

  // if (forward_connect) {
  // ENVOY_LOG(trace, "TunnelHandlerImpl:: Forwarding CONNECT upstream");
  // Http1::ClientConnectionImpl upstream_http(upstream_conn_data_->connection(),
  //                                          http_conn_callbacks_);
  // Http1::RequestStreamEncoderImpl upstream_request =
  // Http1::RequestStreamEncoderImpl(upstream_http);
  /// upstream_request.encodeHeaders(request_headers_, false);
  //} else {
  // The upstream host is the origin server
  // Tell downstream we're good to go
  ENVOY_LOG(trace,
            "TunnelHandlerImpl:: We are connected directly to the origin; sending 200 downstream");
  HeaderMapImpl headers{{Headers::get().Status, std::to_string(200)}};
  tunnel_callbacks_.sendHeadersOnlyResponse(headers, false);
  //}
  ASSERT(state_ == ConnectState::PreConnect);
  state_ = ConnectState::Connected;
  if (queued_data_.length() > 0 || queued_end_stream_) {
    ENVOY_LOG(trace, "TunnelHandlerImpl::onConnectionSuccess calling TcpProxy::onData");
    TcpProxy::Filter::onData(queued_data_, queued_end_stream_);
    ASSERT(queued_data_.length() == 0);
  }
}

StreamInfo::StreamInfo& TunnelHandlerImpl::getStreamInfo() { return stream_info_; }

} // namespace Tunnel
} // namespace Http
} // namespace Envoy
