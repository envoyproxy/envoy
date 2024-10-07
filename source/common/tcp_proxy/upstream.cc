#include "source/common/tcp_proxy/upstream.h"

#include "envoy/http/header_map.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/http/codec_client.h"
#include "source/common/http/codes.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/http/null_route_impl.h"
#include "source/common/http/utility.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace TcpProxy {

using TunnelingConfig =
    envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy_TunnelingConfig;

TcpUpstream::TcpUpstream(Tcp::ConnectionPool::ConnectionDataPtr&& data,
                         Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks)
    : upstream_conn_data_(std::move(data)) {
  Network::ClientConnection& connection = upstream_conn_data_->connection();
  connection.enableHalfClose(true);
  upstream_conn_data_->addUpstreamCallbacks(upstream_callbacks);
}

bool TcpUpstream::readDisable(bool disable) {
  if (upstream_conn_data_ == nullptr ||
      upstream_conn_data_->connection().state() != Network::Connection::State::Open) {
    // Because we flush write downstream, we can have a case where upstream has already disconnected
    // and we are waiting to flush. If we had a watermark event during this time we should no
    // longer touch the upstream connection.
    return false;
  }

  upstream_conn_data_->connection().readDisable(disable);
  return true;
}

void TcpUpstream::encodeData(Buffer::Instance& data, bool end_stream) {
  upstream_conn_data_->connection().write(data, end_stream);
}

void TcpUpstream::addBytesSentCallback(Network::Connection::BytesSentCb cb) {
  upstream_conn_data_->connection().addBytesSentCallback(cb);
}

bool TcpUpstream::startUpstreamSecureTransport() {
  return (upstream_conn_data_ == nullptr)
             ? false
             : upstream_conn_data_->connection().startSecureTransport();
}

Ssl::ConnectionInfoConstSharedPtr TcpUpstream::getUpstreamConnectionSslInfo() {
  if (upstream_conn_data_ != nullptr) {
    return upstream_conn_data_->connection().ssl();
  }
  return nullptr;
}

Tcp::ConnectionPool::ConnectionData*
TcpUpstream::onDownstreamEvent(Network::ConnectionEvent event) {
  // TODO(botengyao): propagate RST back to upstream connection if RST is received from downstream.
  if (event == Network::ConnectionEvent::RemoteClose) {
    // The close call may result in this object being deleted. Latch the
    // connection locally so it can be returned for potential draining.
    auto* conn_data = upstream_conn_data_.release();
    conn_data->connection().close(
        Network::ConnectionCloseType::FlushWrite,
        StreamInfo::LocalCloseReasons::get().ClosingUpstreamTcpDueToDownstreamRemoteClose);
    return conn_data;
  } else if (event == Network::ConnectionEvent::LocalClose) {
    upstream_conn_data_->connection().close(
        Network::ConnectionCloseType::NoFlush,
        StreamInfo::LocalCloseReasons::get().ClosingUpstreamTcpDueToDownstreamLocalClose);
  }
  return nullptr;
}

HttpUpstream::HttpUpstream(Tcp::ConnectionPool::UpstreamCallbacks& callbacks,
                           const TunnelingConfigHelper& config,
                           StreamInfo::StreamInfo& downstream_info, Http::CodecType type)
    : config_(config), downstream_info_(downstream_info), response_decoder_(*this),
      upstream_callbacks_(callbacks), type_(type) {}

HttpUpstream::~HttpUpstream() { resetEncoder(Network::ConnectionEvent::LocalClose); }

bool HttpUpstream::isValidResponse(const Http::ResponseHeaderMap& headers) {
  if (type_ == Http::CodecType::HTTP1) {
    //  According to RFC7231 any 2xx response indicates that the connection is
    //  established.
    //  Any 'Content-Length' or 'Transfer-Encoding' header fields MUST be ignored.
    //  https://tools.ietf.org/html/rfc7231#section-4.3.6
    return Http::CodeUtility::is2xx(Http::Utility::getResponseStatus(headers));
  }
  return Http::Utility::getResponseStatus(headers) == 200;
}

void HttpUpstream::setRequestEncoder(Http::RequestEncoder& request_encoder, bool is_ssl) {
  request_encoder_ = &request_encoder;
  request_encoder_->getStream().addCallbacks(*this);
  auto headers = Http::createHeaderMap<Http::RequestHeaderMapImpl>({
      {Http::Headers::get().Method, config_.usePost() ? "POST" : "CONNECT"},
      {Http::Headers::get().Host, config_.host(downstream_info_)},
  });
  if (config_.usePost()) {
    headers->addReference(Http::Headers::get().Path, config_.postPath());
  }

  if (type_ == Http::CodecType::HTTP1) {
    request_encoder_->enableTcpTunneling();
    ASSERT(request_encoder_->http1StreamEncoderOptions() != absl::nullopt);
  } else {
    const std::string& scheme =
        is_ssl ? Http::Headers::get().SchemeValues.Https : Http::Headers::get().SchemeValues.Http;

    if (config_.usePost()) {
      headers->addReference(Http::Headers::get().Scheme, scheme);
    }
  }

  config_.headerEvaluator().evaluateHeaders(*headers, {downstream_info_.getRequestHeaders()},
                                            downstream_info_);
  const auto status = request_encoder_->encodeHeaders(*headers, false);
  // Encoding can only fail on missing required request headers.
  ASSERT(status.ok());
}

bool HttpUpstream::readDisable(bool disable) {
  if (!request_encoder_) {
    return false;
  }
  request_encoder_->getStream().readDisable(disable);
  return true;
}

void HttpUpstream::encodeData(Buffer::Instance& data, bool end_stream) {
  if (!request_encoder_) {
    return;
  }
  auto codec = type_;
  request_encoder_->encodeData(data, end_stream);

  // doneWriting() is being skipped for H1 codec to avoid resetEncoder() call.
  // This is because H1 codec does not support half-closed stream. Calling resetEncoder()
  // will fully close the upstream connection without flushing any pending data, rather than a http
  // stream reset.
  // More details can be found on https://github.com/envoyproxy/envoy/pull/13293
  if ((codec != Http::CodecType::HTTP1) && (end_stream)) {
    doneWriting();
  }
}

void HttpUpstream::addBytesSentCallback(Network::Connection::BytesSentCb) {
  // The HTTP tunneling mode does not tickle the idle timeout when bytes are
  // sent to the kernel.
  // This can be implemented if any user cares about the difference in time
  // between it being sent to the HTTP/2 stack and out to the kernel.
}

Tcp::ConnectionPool::ConnectionData*
HttpUpstream::onDownstreamEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::LocalClose ||
      event == Network::ConnectionEvent::RemoteClose) {
    resetEncoder(Network::ConnectionEvent::LocalClose, false);
  }
  return nullptr;
}

void HttpUpstream::onResetStream(Http::StreamResetReason, absl::string_view) {
  read_half_closed_ = true;
  write_half_closed_ = true;
  resetEncoder(Network::ConnectionEvent::LocalClose);
}

void HttpUpstream::onAboveWriteBufferHighWatermark() {
  upstream_callbacks_.onAboveWriteBufferHighWatermark();
}

void HttpUpstream::onBelowWriteBufferLowWatermark() {
  upstream_callbacks_.onBelowWriteBufferLowWatermark();
}

void HttpUpstream::resetEncoder(Network::ConnectionEvent event, bool inform_downstream) {
  if (!request_encoder_) {
    return;
  }
  request_encoder_->getStream().removeCallbacks(*this);
  if (!write_half_closed_ || !read_half_closed_) {
    request_encoder_->getStream().resetStream(Http::StreamResetReason::LocalReset);
  }
  request_encoder_ = nullptr;
  // If we did not receive a valid CONNECT response yet we treat this as a pool
  // failure, otherwise we forward the event downstream.
  if (conn_pool_callbacks_ != nullptr) {
    conn_pool_callbacks_->onFailure();
    return;
  }

  if (inform_downstream) {
    upstream_callbacks_.onEvent(event);
  }
}

void HttpUpstream::doneReading() {
  read_half_closed_ = true;
  if (write_half_closed_) {
    resetEncoder(Network::ConnectionEvent::LocalClose);
  }
}

void HttpUpstream::doneWriting() {
  write_half_closed_ = true;
  if (read_half_closed_) {
    resetEncoder(Network::ConnectionEvent::LocalClose);
  }
}

TcpConnPool::TcpConnPool(Upstream::ThreadLocalCluster& thread_local_cluster,
                         Upstream::LoadBalancerContext* context,
                         Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks,
                         StreamInfo::StreamInfo& downstream_info)
    : upstream_callbacks_(upstream_callbacks), downstream_info_(downstream_info) {
  conn_pool_data_ = thread_local_cluster.tcpConnPool(Upstream::ResourcePriority::Default, context);
}

TcpConnPool::~TcpConnPool() {
  if (upstream_handle_ != nullptr) {
    upstream_handle_->cancel(ConnectionPool::CancelPolicy::CloseExcess);
  }
}

void TcpConnPool::newStream(GenericConnectionPoolCallbacks& callbacks) {
  callbacks_ = &callbacks;
  // Given this function is re-entrant, make sure we only reset the upstream_handle_ if given a
  // valid connection handle. If newConnection fails inline it may result in attempting to
  // select a new host, and a recursive call to establishUpstreamConnection. In this case the
  // first call to newConnection will return null and the inner call will persist.
  Tcp::ConnectionPool::Cancellable* handle = conn_pool_data_.value().newConnection(*this);
  if (handle) {
    ASSERT(upstream_handle_ == nullptr);
    upstream_handle_ = handle;
  }
}

void TcpConnPool::onPoolFailure(ConnectionPool::PoolFailureReason reason,
                                absl::string_view failure_reason,
                                Upstream::HostDescriptionConstSharedPtr host) {
  upstream_handle_ = nullptr;
  callbacks_->onGenericPoolFailure(reason, failure_reason, host);
}

void TcpConnPool::onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                              Upstream::HostDescriptionConstSharedPtr host) {
  if (downstream_info_.downstreamAddressProvider().connectionID()) {
    downstream_info_.upstreamInfo()->setUpstreamConnectionId(conn_data->connection().id());
    ENVOY_LOG(debug, "Attached upstream connection [C{}] to downstream connection [C{}]",
              conn_data->connection().id(),
              downstream_info_.downstreamAddressProvider().connectionID().value());
  }

  upstream_handle_ = nullptr;
  Tcp::ConnectionPool::ConnectionData* latched_data = conn_data.get();
  Network::Connection& connection = conn_data->connection();

  auto upstream = std::make_unique<TcpUpstream>(std::move(conn_data), upstream_callbacks_);
  callbacks_->onGenericPoolReady(
      &connection.streamInfo(), std::move(upstream), host,
      latched_data->connection().connectionInfoProvider(),
      latched_data->connection().streamInfo().downstreamAddressProvider().sslConnection());
}

HttpConnPool::HttpConnPool(Upstream::ThreadLocalCluster& thread_local_cluster,
                           Upstream::LoadBalancerContext* context,
                           const TunnelingConfigHelper& config,
                           Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks,
                           Http::StreamDecoderFilterCallbacks& stream_decoder_callbacks,
                           Http::CodecType type, StreamInfo::StreamInfo& downstream_info)
    : config_(config), type_(type), decoder_filter_callbacks_(&stream_decoder_callbacks),
      upstream_callbacks_(upstream_callbacks), downstream_info_(downstream_info) {
  absl::optional<Http::Protocol> protocol;
  if (type_ == Http::CodecType::HTTP3) {
    protocol = Http::Protocol::Http3;
  } else if (type_ == Http::CodecType::HTTP2) {
    protocol = Http::Protocol::Http2;
  }
  if (Runtime::runtimeFeatureEnabled(
          "envoy.restart_features.upstream_http_filters_with_tcp_proxy")) {
    absl::optional<Envoy::Http::Protocol> upstream_protocol = protocol;
    generic_conn_pool_ = createConnPool(thread_local_cluster, context, upstream_protocol);
    return;
  }
  conn_pool_data_ =
      thread_local_cluster.httpConnPool(Upstream::ResourcePriority::Default, protocol, context);
}

std::unique_ptr<Router::GenericConnPool>
HttpConnPool::createConnPool(Upstream::ThreadLocalCluster& cluster,
                             Upstream::LoadBalancerContext* context,
                             absl::optional<Http::Protocol> protocol) {
  Router::GenericConnPoolFactory* factory = nullptr;
  factory = Envoy::Config::Utility::getFactoryByName<Router::GenericConnPoolFactory>(
      "envoy.filters.connection_pools.http.generic");
  if (!factory) {
    return nullptr;
  }

  return factory->createGenericConnPool(
      cluster, Envoy::Router::GenericConnPoolFactory::UpstreamProtocol::HTTP,
      decoder_filter_callbacks_->route()->routeEntry()->priority(), protocol, context);
}

HttpConnPool::~HttpConnPool() {
  if (upstream_handle_ != nullptr) {
    // Because HTTP connections are generally shorter lived and have a higher probability of use
    // before going idle, they are closed with Default rather than CloseExcess.
    upstream_handle_->cancel(ConnectionPool::CancelPolicy::Default);
  }
  if (combined_upstream_ != nullptr) {
    combined_upstream_->onDownstreamEvent(Network::ConnectionEvent::LocalClose);
  }
}

void HttpConnPool::newStream(GenericConnectionPoolCallbacks& callbacks) {
  callbacks_ = &callbacks;
  if (Runtime::runtimeFeatureEnabled(
          "envoy.restart_features.upstream_http_filters_with_tcp_proxy")) {
    combined_upstream_ = std::make_unique<CombinedUpstream>(
        *this, upstream_callbacks_, *decoder_filter_callbacks_, config_, downstream_info_);
    RouterUpstreamRequestPtr upstream_request = std::make_unique<RouterUpstreamRequest>(
        *combined_upstream_, std::move(generic_conn_pool_), /*can_send_early_data_=*/false,
        /*can_use_http3_=*/true, true /*enable_tcp_tunneling*/);
    combined_upstream_->setRouterUpstreamRequest(std::move(upstream_request));
    combined_upstream_->newStream(callbacks);
    return;
  }

  upstream_ = std::make_unique<HttpUpstream>(upstream_callbacks_, config_, downstream_info_, type_);
  Tcp::ConnectionPool::Cancellable* handle =
      conn_pool_data_.value().newStream(upstream_->responseDecoder(), *this,
                                        {/*can_send_early_data_=*/false,
                                         /*can_use_http3_=*/true});
  if (handle != nullptr) {
    upstream_handle_ = handle;
  }
}

void HttpConnPool::onPoolFailure(ConnectionPool::PoolFailureReason reason,
                                 absl::string_view failure_reason,
                                 Upstream::HostDescriptionConstSharedPtr host) {
  upstream_handle_ = nullptr;
  callbacks_->onGenericPoolFailure(reason, failure_reason, host);
}

void HttpConnPool::onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host,
                                          bool pool_success) {
  if (!pool_success) {
    return;
  }
  combined_upstream_->setConnPoolCallbacks(std::make_unique<HttpConnPool::Callbacks>(
      *this, host, downstream_info_.downstreamAddressProvider().sslConnection()));
}

void HttpConnPool::onPoolReady(Http::RequestEncoder& request_encoder,
                               Upstream::HostDescriptionConstSharedPtr host,
                               StreamInfo::StreamInfo& info, absl::optional<Http::Protocol>) {
  if (info.downstreamAddressProvider().connectionID() &&
      downstream_info_.downstreamAddressProvider().connectionID()) {
    // info.downstreamAddressProvider() is being called to get the upstream connection ID,
    // because the StreamInfo object here is of the upstream connection.
    downstream_info_.upstreamInfo()->setUpstreamConnectionId(
        info.downstreamAddressProvider().connectionID().value());
    ENVOY_LOG(debug, "Attached upstream connection [C{}] to downstream connection [C{}]",
              info.downstreamAddressProvider().connectionID().value(),
              downstream_info_.downstreamAddressProvider().connectionID().value());
  }

  upstream_handle_ = nullptr;
  upstream_->setRequestEncoder(request_encoder,
                               host->transportSocketFactory().implementsSecureTransport());
  upstream_->setConnPoolCallbacks(std::make_unique<HttpConnPool::Callbacks>(
      *this, host, info.downstreamAddressProvider().sslConnection()));
}

void HttpConnPool::onGenericPoolReady(Upstream::HostDescriptionConstSharedPtr& host,
                                      const Network::ConnectionInfoProvider& address_provider,
                                      Ssl::ConnectionInfoConstSharedPtr ssl_info) {
  if (Runtime::runtimeFeatureEnabled(
          "envoy.restart_features.upstream_http_filters_with_tcp_proxy")) {

    callbacks_->onGenericPoolReady(nullptr, std::move(combined_upstream_), host, address_provider,
                                   ssl_info);
    return;
  }
  callbacks_->onGenericPoolReady(nullptr, std::move(upstream_), host, address_provider, ssl_info);
}

CombinedUpstream::CombinedUpstream(HttpConnPool& http_conn_pool,
                                   Tcp::ConnectionPool::UpstreamCallbacks& callbacks,
                                   Http::StreamDecoderFilterCallbacks& decoder_callbacks,
                                   const TunnelingConfigHelper& config,
                                   StreamInfo::StreamInfo& downstream_info)
    : config_(config), downstream_info_(downstream_info), parent_(http_conn_pool),
      decoder_filter_callbacks_(decoder_callbacks), response_decoder_(*this),
      upstream_callbacks_(callbacks) {
  auto is_ssl = downstream_info_.downstreamAddressProvider().sslConnection();
  downstream_headers_ = Http::createHeaderMap<Http::RequestHeaderMapImpl>({
      {Http::Headers::get().Method, config_.usePost() ? "POST" : "CONNECT"},
      {Http::Headers::get().Host, config_.host(downstream_info_)},
  });

  if (config_.usePost()) {
    const std::string& scheme =
        is_ssl ? Http::Headers::get().SchemeValues.Https : Http::Headers::get().SchemeValues.Http;
    downstream_headers_->addReference(Http::Headers::get().Path, config_.postPath());
    downstream_headers_->addReference(Http::Headers::get().Scheme, scheme);
  }

  config_.headerEvaluator().evaluateHeaders(
      *downstream_headers_, {downstream_info_.getRequestHeaders()}, downstream_info_);
}

void CombinedUpstream::setRouterUpstreamRequest(
    Router::UpstreamRequestPtr router_upstream_request) {
  ASSERT(!upstream_request_);
  upstream_request_ = std::move(router_upstream_request);
}

void CombinedUpstream::newStream(GenericConnectionPoolCallbacks&) {
  upstream_request_->acceptHeadersFromRouter(false);
}

void CombinedUpstream::encodeData(Buffer::Instance& data, bool end_stream) {
  if (!upstream_request_) {
    return;
  }
  upstream_request_->acceptDataFromRouter(data, end_stream);
  if (end_stream) {
    doneWriting();
  }
}

bool CombinedUpstream::readDisable(bool disable) {
  if (!upstream_request_) {
    return false;
  }
  upstream_request_->readDisableOrDefer(disable);
  return true;
}

Tcp::ConnectionPool::ConnectionData*
CombinedUpstream::onDownstreamEvent(Network::ConnectionEvent event) {
  if (!upstream_request_) {
    return nullptr;
  }

  if (event == Network::ConnectionEvent::LocalClose ||
      event == Network::ConnectionEvent::RemoteClose) {
    upstream_request_->resetStream();
  }
  return nullptr;
}

bool CombinedUpstream::isValidResponse(const Http::ResponseHeaderMap& headers) {
  switch (parent_.codecType()) {
  case Http::CodecType::HTTP1:
    // According to RFC7231 any 2xx response indicates that the connection is
    // established.
    // Any 'Content-Length' or 'Transfer-Encoding' header fields MUST be ignored.
    // https://tools.ietf.org/html/rfc7231#section-4.3.6
    return Http::CodeUtility::is2xx(Http::Utility::getResponseStatus(headers));
  case Http::CodecType::HTTP2:
  case Http::CodecType::HTTP3:
    if (Http::Utility::getResponseStatus(headers) != 200) {
      return false;
    }
    return true;
  }
  return true;
}

void CombinedUpstream::onResetEncoder(Network::ConnectionEvent event, bool inform_downstream) {
  if (event == Network::ConnectionEvent::LocalClose ||
      event == Network::ConnectionEvent::RemoteClose) {
    if (upstream_request_) {
      upstream_request_->resetStream();
    }
  }

  // If we did not receive a valid CONNECT response yet we treat this as a pool
  // failure, otherwise we forward the event downstream.
  if (conn_pool_callbacks_ != nullptr) {
    conn_pool_callbacks_->onFailure();
    return;
  }

  if (inform_downstream) {
    upstream_callbacks_.onEvent(event);
  }
}

// Router::RouterFilterInterface
void CombinedUpstream::onUpstreamHeaders([[maybe_unused]] uint64_t response_code,
                                         Http::ResponseHeaderMapPtr&& headers,
                                         [[maybe_unused]] UpstreamRequest& upstream_request,
                                         bool end_stream) {
  responseDecoder().decodeHeaders(std::move(headers), end_stream);
}

void CombinedUpstream::onUpstreamData(Buffer::Instance& data,
                                      [[maybe_unused]] UpstreamRequest& upstream_request,
                                      bool end_stream) {
  responseDecoder().decodeData(data, end_stream);
}

void CombinedUpstream::onUpstreamTrailers(Http::ResponseTrailerMapPtr&& trailers,
                                          UpstreamRequest&) {
  responseDecoder().decodeTrailers(std::move(trailers));
}

Http::RequestHeaderMap* CombinedUpstream::downstreamHeaders() { return downstream_headers_.get(); }

void CombinedUpstream::doneReading() {
  read_half_closed_ = true;
  if (write_half_closed_) {
    onResetEncoder(Network::ConnectionEvent::LocalClose);
  }
}

void CombinedUpstream::onUpstreamReset(Http::StreamResetReason, absl::string_view,
                                       UpstreamRequest&) {
  upstream_callbacks_.onEvent(Network::ConnectionEvent::RemoteClose);
}

void CombinedUpstream::doneWriting() {
  write_half_closed_ = true;
  if (read_half_closed_) {
    onResetEncoder(Network::ConnectionEvent::LocalClose);
  }
}
} // namespace TcpProxy
} // namespace Envoy
