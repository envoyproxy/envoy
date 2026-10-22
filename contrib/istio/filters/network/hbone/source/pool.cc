#include "contrib/istio/filters/network/hbone/source/pool.h"

#include "envoy/router/router_filter_interface.h"

#include "source/common/config/utility.h"

#include "contrib/istio/filters/network/hbone/source/hbone.h"

namespace Envoy {
namespace Extensions {
namespace Istio {
namespace Hbone {
namespace {

void recordRejection(StreamInfo::StreamInfo& info, absl::string_view reason,
                     Upstream::HostDescriptionConstSharedPtr host) {
  if (info.upstreamInfo()) {
    info.upstreamInfo()->setUpstreamHost(host);
    info.upstreamInfo()->setUpstreamTransportFailureReason(reason);
  }
  info.setResponseFlag(StreamInfo::CoreResponseFlag::UpstreamConnectionFailure);
  info.setResponseCodeDetails(reason);
}

// Intercept only failed establishment. Successful pools and application responses
// retain the stock router behavior, including application-level retry policies.
class HttpPool : public Router::GenericConnPool, public Router::GenericConnectionPoolCallbacks {
public:
  HttpPool(Router::GenericConnPoolPtr inner, Http::StreamDecoderFilterCallbacks& downstream)
      : inner_(std::move(inner)), downstream_(downstream),
        reject_(downstream.dispatcher().createSchedulableCallback([this]() {
          const std::string reason = failure_reason_;
          const auto status = *permanentConnectRejection(reason);
          // Other invalid CONNECT responses (e.g. 302) are gateway protocol
          // failures, not redirects or successful application responses.
          const auto code = status >= 400 && status < 500 ? static_cast<Http::Code>(status)
                                                          : Http::Code::BadGateway;
          downstream_.sendLocalReply(code, "HBONE CONNECT rejected", nullptr, std::nullopt, reason);
        })) {}
  ~HttpPool() override { cancelAnyPendingStream(); }

  void newStream(Router::GenericConnectionPoolCallbacks* callbacks) override {
    callbacks_ = callbacks;
    inner_->newStream(this);
  }
  bool cancelAnyPendingStream() override {
    const bool rejecting = reject_->enabled();
    reject_->cancel();
    return inner_->cancelAnyPendingStream() || rejecting;
  }
  bool valid() const override { return inner_->valid(); }
  Upstream::HostDescriptionConstSharedPtr host() const override { return inner_->host(); }
  Router::UpstreamToDownstream& upstreamToDownstream() override {
    return callbacks_->upstreamToDownstream();
  }
  void onPoolReady(std::unique_ptr<Router::GenericUpstream>&& upstream,
                   Upstream::HostDescriptionConstSharedPtr host,
                   const Network::ConnectionInfoProvider& address_provider,
                   StreamInfo::StreamInfo& info, std::optional<Http::Protocol> protocol) override {
    callbacks_->onPoolReady(std::move(upstream), host, address_provider, info, protocol);
  }
  void onPoolFailure(ConnectionPool::PoolFailureReason reason, absl::string_view failure_reason,
                     Upstream::HostDescriptionConstSharedPtr host) override {
    if (!permanentConnectRejection(failure_reason)) {
      callbacks_->onPoolFailure(reason, failure_reason, host);
      return;
    }
    failure_reason_ = std::string(failure_reason);
    recordRejection(downstream_.streamInfo(), failure_reason_, host);
    // Do not synthesize a ready connection or an upstream HTTP response. Both
    // would enter router accounting and could enable application forwarding or
    // response-code retries. The local reply terminates all pending attempts.
    reject_->scheduleCallbackNextIteration();
  }

private:
  Router::GenericConnPoolPtr inner_;
  Http::StreamDecoderFilterCallbacks& downstream_;
  Event::SchedulableCallbackPtr reject_;
  Router::GenericConnectionPoolCallbacks* callbacks_{};
  std::string failure_reason_;
};

class TcpPool : public TcpProxy::GenericConnPool, public TcpProxy::GenericConnectionPoolCallbacks {
public:
  TcpPool(TcpProxy::GenericConnPoolPtr inner, Network::Connection& downstream,
          StreamInfo::StreamInfo& info)
      : inner_(std::move(inner)), downstream_(downstream), info_(info),
        reject_(downstream.dispatcher().createSchedulableCallback([this]() {
          // Closing can destroy this pool. Keep the detail alive until close returns.
          const std::string reason = failure_reason_;
          downstream_.close(Network::ConnectionCloseType::NoFlush, reason);
        })) {}
  ~TcpPool() override { reject_->cancel(); }
  void newStream(TcpProxy::GenericConnectionPoolCallbacks& callbacks) override {
    callbacks_ = &callbacks;
    inner_->newStream(*this);
  }
  void onGenericPoolReady(StreamInfo::StreamInfo* info,
                          std::unique_ptr<TcpProxy::GenericUpstream>&& upstream,
                          Upstream::HostDescriptionConstSharedPtr& host,
                          const Network::ConnectionInfoProvider& address_provider,
                          Ssl::ConnectionInfoConstSharedPtr ssl_info) override {
    callbacks_->onGenericPoolReady(info, std::move(upstream), host, address_provider, ssl_info);
  }
  void onGenericPoolFailure(ConnectionPool::PoolFailureReason reason,
                            absl::string_view failure_reason,
                            Upstream::HostDescriptionConstSharedPtr host) override {
    if (!permanentConnectRejection(failure_reason)) {
      callbacks_->onGenericPoolFailure(reason, failure_reason, host);
      return;
    }
    failure_reason_ = std::string(failure_reason);
    recordRejection(info_, failure_reason_, host);
    // In particular, do not call TCP proxy's stub HTTP sendLocalReply callback.
    reject_->scheduleCallbackNextIteration();
  }

private:
  TcpProxy::GenericConnPoolPtr inner_;
  Network::Connection& downstream_;
  StreamInfo::StreamInfo& info_;
  Event::SchedulableCallbackPtr reject_;
  TcpProxy::GenericConnectionPoolCallbacks* callbacks_{};
  std::string failure_reason_;
};

} // namespace

Router::GenericConnPoolPtr HttpPoolFactory::createGenericConnPool(
    Upstream::HostConstSharedPtr host, Upstream::ThreadLocalCluster& cluster,
    UpstreamProtocol protocol, Upstream::ResourcePriority priority,
    std::optional<Http::Protocol> downstream_protocol, Upstream::LoadBalancerContext* context,
    const Protobuf::Message& config) const {
  const auto* factory = Envoy::Config::Utility::getFactoryByName<Router::GenericConnPoolFactory>(
      "envoy.filters.connection_pools.http.generic");
  ASSERT(factory != nullptr);
  auto inner = factory->createGenericConnPool(host, cluster, protocol, priority,
                                              downstream_protocol, context, config);
  auto* router = dynamic_cast<Router::RouterFilterInterface*>(context);
  if (!inner || router == nullptr) {
    return inner;
  }
  return std::make_unique<HttpPool>(std::move(inner), *router->callbacks());
}

TcpProxy::GenericConnPoolPtr TcpPoolFactory::createGenericConnPool(
    Upstream::HostConstSharedPtr host, Upstream::ThreadLocalCluster& cluster,
    TcpProxy::TunnelingConfigHelperOptConstRef config, Upstream::LoadBalancerContext* context,
    Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks,
    Http::StreamDecoderFilterCallbacks& decoder_callbacks,
    StreamInfo::StreamInfo& downstream_info) const {
  const auto* factory = Envoy::Config::Utility::getFactoryByName<TcpProxy::GenericConnPoolFactory>(
      "envoy.filters.connection_pools.tcp.generic");
  ASSERT(factory != nullptr);
  auto inner = factory->createGenericConnPool(host, cluster, config, context, upstream_callbacks,
                                              decoder_callbacks, downstream_info);
  if (!inner || context == nullptr || context->downstreamConnection() == nullptr) {
    return inner;
  }
  // LoadBalancerContext exposes a const view of the TCP proxy's mutable network
  // connection. Closing that connection is the TCP equivalent of a local reply.
  return std::make_unique<TcpPool>(
      std::move(inner), const_cast<Network::Connection&>(*context->downstreamConnection()),
      downstream_info);
}

REGISTER_FACTORY(HttpPoolFactory, Router::GenericConnPoolFactory);
REGISTER_FACTORY(TcpPoolFactory, TcpProxy::GenericConnPoolFactory);

} // namespace Hbone
} // namespace Istio
} // namespace Extensions
} // namespace Envoy
