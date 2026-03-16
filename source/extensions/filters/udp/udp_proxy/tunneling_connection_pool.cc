#include "source/extensions/filters/udp/udp_proxy/tunneling_connection_pool.h"

#include "envoy/stream_info/uint32_accessor.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {

void HttpUpstreamImpl::encodeData(Buffer::Instance& data) {
  if (!request_encoder_) {
    return;
  }

  request_encoder_->encodeData(data, false);
}

void HttpUpstreamImpl::setRequestEncoder(Http::RequestEncoder& request_encoder, bool is_ssl) {
  request_encoder_ = &request_encoder;
  request_encoder_->getStream().addCallbacks(*this);

  const std::string& scheme =
      is_ssl ? Http::Headers::get().SchemeValues.Https : Http::Headers::get().SchemeValues.Http;

  std::string host = tunnel_config_.proxyHost(downstream_info_);
  const auto* dynamic_port =
      downstream_info_.filterState()->getDataReadOnly<StreamInfo::UInt32Accessor>(
          "udp.connect.proxy_port");
  if (dynamic_port != nullptr && dynamic_port->value() > 0 && dynamic_port->value() <= 65535) {
    absl::StrAppend(&host, ":", std::to_string(dynamic_port->value()));
  } else if (tunnel_config_.proxyPort().has_value()) {
    absl::StrAppend(&host, ":", std::to_string(tunnel_config_.proxyPort().value()));
  }

  auto headers = Http::RequestHeaderMapImpl::create();
  headers->addReferenceKey(Http::Headers::get().Scheme, scheme);
  headers->addReferenceKey(Http::Headers::get().Host, host);

  if (tunnel_config_.usePost()) {
    headers->addReferenceKey(Http::Headers::get().Method, "POST");
    headers->addReferenceKey(Http::Headers::get().Path, tunnel_config_.postPath());
  } else {
    // The Envoy HTTP/2 and HTTP/3 clients expect the request header map to be in the form of HTTP/1
    // upgrade to issue an extended CONNECT request.
    headers->addReferenceKey(Http::Headers::get().Method, "GET");
    headers->addReferenceKey(Http::Headers::get().Connection, "Upgrade");
    headers->addReferenceKey(Http::Headers::get().Upgrade, "connect-udp");
    headers->addReferenceKey(Http::Headers::get().CapsuleProtocol, "?1");
    const std::string target_tunnel_path = resolveTargetTunnelPath();
    headers->addReferenceKey(Http::Headers::get().Path, target_tunnel_path);
  }

  tunnel_config_.headerEvaluator().evaluateHeaders(*headers, {downstream_info_.getRequestHeaders()},
                                                   downstream_info_);

  const auto status = request_encoder_->encodeHeaders(*headers, false);
  // Encoding can only fail on missing required request headers.
  ASSERT(status.ok());
}

const std::string HttpUpstreamImpl::resolveTargetTunnelPath() {
  std::string target_host = tunnel_config_.targetHost(downstream_info_);
  target_host = Http::Utility::PercentEncoding::encode(target_host, ":");

  const auto* dynamic_port =
      downstream_info_.filterState()->getDataReadOnly<StreamInfo::UInt32Accessor>(
          "udp.connect.target_port");

  std::string target_port;
  if (dynamic_port != nullptr && dynamic_port->value() > 0 && dynamic_port->value() <= 65535) {
    target_port = std::to_string(dynamic_port->value());
  } else {
    target_port = std::to_string(tunnel_config_.defaultTargetPort());
  }

  // TODO(ohadvano): support configurable URI template.
  return absl::StrCat("/.well-known/masque/udp/", target_host, "/", target_port, "/");
}

HttpUpstreamImpl::~HttpUpstreamImpl() {
  resetEncoder(Network::ConnectionEvent::LocalClose, /*by_local_close=*/true);
}

void HttpUpstreamImpl::resetEncoder(Network::ConnectionEvent event, bool by_local_close) {
  if (!request_encoder_) {
    return;
  }

  request_encoder_->getStream().removeCallbacks(*this);
  if (by_local_close) {
    request_encoder_->getStream().resetStream(Http::StreamResetReason::LocalReset);
  }

  request_encoder_ = nullptr;

  if (!by_local_close) {
    // If we did not receive a valid CONNECT response yet we treat this as a pool
    // failure, otherwise we forward the event downstream.
    if (tunnel_creation_callbacks_.has_value()) {
      tunnel_creation_callbacks_.value().get().onStreamFailure();
      return;
    }

    upstream_callbacks_.onUpstreamEvent(event);
  }
}

TunnelingConnectionPoolImpl::TunnelingConnectionPoolImpl(
    Upstream::ThreadLocalCluster& thread_local_cluster, Upstream::LoadBalancerContext* context,
    const UdpTunnelingConfig& tunnel_config, UpstreamTunnelCallbacks& upstream_callbacks,
    StreamInfo::StreamInfo& downstream_info)
    : upstream_callbacks_(upstream_callbacks), tunnel_config_(tunnel_config),
      downstream_info_(downstream_info) {
  // TODO(ohadvano): support upstream HTTP/3.
  absl::optional<Http::Protocol> protocol = Http::Protocol::Http2;
  auto host = Upstream::LoadBalancer::onlyAllowSynchronousHostSelection(
      thread_local_cluster.loadBalancer().chooseHost(context));
  conn_pool_data_ = thread_local_cluster.httpConnPool(host, Upstream::ResourcePriority::Default,
                                                      protocol, context);
}

void TunnelingConnectionPoolImpl::newStream(HttpStreamCallbacks& callbacks) {
  callbacks_ = &callbacks;
  upstream_ =
      std::make_unique<HttpUpstreamImpl>(upstream_callbacks_, tunnel_config_, downstream_info_);
  Tcp::ConnectionPool::Cancellable* handle =
      conn_pool_data_.value().newStream(upstream_->responseDecoder(), *this,
                                        {/*can_send_early_data_=*/false,
                                         /*can_use_http3_=*/false});

  if (handle != nullptr) {
    upstream_handle_ = handle;
  }
}

void TunnelingConnectionPoolImpl::onPoolFailure(Http::ConnectionPool::PoolFailureReason reason,
                                                absl::string_view failure_reason,
                                                Upstream::HostDescriptionConstSharedPtr host) {
  upstream_handle_ = nullptr;
  // Writing to downstream_info_ before calling onStreamFailure, as the session could be potentially
  // removed by onStreamFailure, which will cause downstream_info_ to be freed.
  downstream_info_.upstreamInfo()->setUpstreamHost(host);
  downstream_info_.upstreamInfo()->setUpstreamTransportFailureReason(failure_reason);
  callbacks_->onStreamFailure(reason, failure_reason, *host);
}

void TunnelingConnectionPoolImpl::onPoolReady(Http::RequestEncoder& request_encoder,
                                              Upstream::HostDescriptionConstSharedPtr upstream_host,
                                              StreamInfo::StreamInfo& upstream_info,
                                              absl::optional<Http::Protocol>) {
  auto upstream_connection_id = upstream_info.downstreamAddressProvider().connectionID().value();
  ENVOY_LOG(debug, "Upstream connection [C{}] ready, creating tunnel stream",
            upstream_connection_id);

  upstream_handle_ = nullptr;
  upstream_host_ = upstream_host;
  upstream_info_ = &upstream_info;
  ssl_info_ = upstream_info.downstreamAddressProvider().sslConnection();

  bool is_ssl = upstream_host->transportSocketFactory().implementsSecureTransport();
  upstream_->setRequestEncoder(request_encoder, is_ssl);
  upstream_->setTunnelCreationCallbacks(*this);
  downstream_info_.upstreamInfo()->setUpstreamHost(upstream_host);
  downstream_info_.setUpstreamBytesMeter(request_encoder.getStream().bytesMeter());
  downstream_info_.upstreamInfo()->setUpstreamConnectionId(upstream_connection_id);
  callbacks_->resetIdleTimer();
}

TunnelingConnectionPoolPtr TunnelingConnectionPoolFactory::createConnPool(
    Upstream::ThreadLocalCluster& thread_local_cluster, Upstream::LoadBalancerContext* context,
    const UdpTunnelingConfig& tunnel_config, UpstreamTunnelCallbacks& upstream_callbacks,
    StreamInfo::StreamInfo& downstream_info) const {
  auto pool = std::make_unique<TunnelingConnectionPoolImpl>(
      thread_local_cluster, context, tunnel_config, upstream_callbacks, downstream_info);
  return (pool->valid() ? std::move(pool) : nullptr);
}

} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
