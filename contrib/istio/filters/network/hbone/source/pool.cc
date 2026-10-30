#include "contrib/istio/filters/network/hbone/source/pool.h"

#include "envoy/router/router_filter_interface.h"
#include "envoy/stream_info/bool_accessor.h"

#include "source/common/common/assert.h"
#include "source/common/config/utility.h"
#include "source/common/tcp_proxy/upstream.h"

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
      : inner_(std::move(inner)), downstream_(downstream) {}
  ~HttpPool() override { cancelAnyPendingStream(); }

  void newStream(Router::GenericConnectionPoolCallbacks* callbacks) override {
    callbacks_ = callbacks;
    inner_->newStream(this);
  }
  bool cancelAnyPendingStream() override {
    const bool rejecting = reject_ && reject_->enabled();
    if (reject_) {
      reject_->cancel();
    }
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
    // Most request attempts reuse an accepted connection. Allocate the deferred
    // reply only when CONNECT is permanently rejected.
    if (!reject_) {
      reject_ = downstream_.dispatcher().createSchedulableCallback([this]() {
        const std::string reason = failure_reason_;
        const auto status = *permanentConnectRejection(reason);
        // Other invalid CONNECT responses (e.g. 302) are gateway protocol
        // failures, not redirects or successful application responses.
        auto code = status >= 400 && status < 500 ? static_cast<Http::Code>(status)
                                                  : Http::Code::BadGateway;
        // A tunnel authorization denial is not an application credential challenge.
        // Preserve the original CONNECT status in the failure reason.
        if (code == Http::Code::Unauthorized) {
          code = Http::Code::Forbidden;
        }
        downstream_.sendLocalReply(code, "HBONE CONNECT rejected", nullptr, std::nullopt, reason);
      });
    }
    // Do not synthesize a ready connection or an upstream HTTP response. Both
    // would enter router accounting and could enable application forwarding or
    // response-code retries. The local reply terminates all pending attempts;
    // router cancellation also cancels this deferred reply.
    reject_->scheduleCallbackNextIteration();
  }

private:
  Router::GenericConnPoolPtr inner_;
  Http::StreamDecoderFilterCallbacks& downstream_;
  Event::SchedulableCallbackPtr reject_;
  Router::GenericConnectionPoolCallbacks* callbacks_{};
  std::string failure_reason_;
};

// Owned by the pending pool, then by the accepted upstream. The underlying
// tunneling implementation retains a reference to these callbacks after readiness.
class TunnelCallbacks : public Tcp::ConnectionPool::UpstreamCallbacks {
public:
  TunnelCallbacks(Tcp::ConnectionPool::UpstreamCallbacks& inner, Network::Connection& downstream,
                  StreamInfo::StreamInfo& info, ConnectStateSharedPtr state)
      : inner_(inner), downstream_(downstream), info_(info), state_(std::move(state)) {}
  ~TunnelCallbacks() override {
    if (reject_) {
      reject_->cancel();
    }
  }

  const ConnectStateSharedPtr& state() const { return state_; }
  void accepted() { accepted_ = true; }

  void reject(absl::string_view reason, Upstream::HostDescriptionConstSharedPtr host) {
    failure_reason_ = std::string(reason);
    state_->fail(failure_reason_);
    recordRejection(info_, failure_reason_, host);
    if (!reject_) {
      reject_ = downstream_.dispatcher().createSchedulableCallback([this]() {
        const std::string reason = failure_reason_;
        downstream_.close(Network::ConnectionCloseType::NoFlush, reason);
      });
    }
    // Leave the decoder stack before closing and destroying its pending pool.
    reject_->scheduleCallbackNextIteration();
  }

  void onEvent(Network::ConnectionEvent event) override {
    if (event == Network::ConnectionEvent::LocalClose ||
        event == Network::ConnectionEvent::RemoteClose) {
      if (state_->phase() == ConnectState::Phase::Failed &&
          permanentConnectRejection(state_->failureReason())) {
        return;
      }
      // The upstream-filter tunneling implementation reports transport failures
      // here, including rejections propagated through an outer HBONE transport.
      if (state_->phase() == ConnectState::Phase::Pending && info_.upstreamInfo() &&
          permanentConnectRejection(info_.upstreamInfo()->upstreamTransportFailureReason())) {
        reject(info_.upstreamInfo()->upstreamTransportFailureReason(),
               info_.upstreamInfo()->upstreamHost());
        return;
      }
      if (info_.upstreamInfo()) {
        state_->recordAttemptFailure(info_.upstreamInfo()->upstreamTransportFailureReason());
      }
      if (accepted_) {
        state_->fail("hbone upstream closed before CONNECT readiness");
      }
    }
    inner_.onEvent(event);
  }
  void onUpstreamData(Buffer::Instance& data, bool end_stream) override {
    inner_.onUpstreamData(data, end_stream);
  }
  void onAboveWriteBufferHighWatermark() override { inner_.onAboveWriteBufferHighWatermark(); }
  void onBelowWriteBufferLowWatermark() override { inner_.onBelowWriteBufferLowWatermark(); }

private:
  Tcp::ConnectionPool::UpstreamCallbacks& inner_;
  Network::Connection& downstream_;
  StreamInfo::StreamInfo& info_;
  const ConnectStateSharedPtr state_;
  Event::SchedulableCallbackPtr reject_;
  std::string failure_reason_;
  bool accepted_{false};
};

class TunnelUpstream : public TcpProxy::GenericUpstream {
public:
  TunnelUpstream(std::unique_ptr<TcpProxy::GenericUpstream> inner,
                 std::shared_ptr<TunnelCallbacks> callbacks)
      : callbacks_(std::move(callbacks)), inner_(std::move(inner)) {}
  ~TunnelUpstream() override {
    callbacks_->state()->fail("hbone upstream destroyed before CONNECT readiness");
  }
  bool readDisable(bool disable) override { return inner_->readDisable(disable); }
  void encodeData(Buffer::Instance& data, bool end_stream) override {
    inner_->encodeData(data, end_stream);
  }
  void addBytesSentCallback(Network::Connection::BytesSentCb callback) override {
    inner_->addBytesSentCallback(std::move(callback));
  }
  Tcp::ConnectionPool::ConnectionData* onDownstreamEvent(Network::ConnectionEvent event,
                                                         absl::string_view details) override {
    if (event == Network::ConnectionEvent::LocalClose ||
        event == Network::ConnectionEvent::RemoteClose) {
      callbacks_->state()->fail("hbone internal peer closed before CONNECT readiness");
    }
    return inner_->onDownstreamEvent(event, details);
  }
  bool startUpstreamSecureTransport() override { return inner_->startUpstreamSecureTransport(); }
  Ssl::ConnectionInfoConstSharedPtr getUpstreamConnectionSslInfo() override {
    return inner_->getUpstreamConnectionSslInfo();
  }
  StreamInfo::DetectedCloseType detectedCloseType() const override {
    return inner_->detectedCloseType();
  }
  absl::string_view localCloseReason() const override { return inner_->localCloseReason(); }

private:
  // Keep callbacks alive while destroying the upstream that references them.
  const std::shared_ptr<TunnelCallbacks> callbacks_;
  std::unique_ptr<TcpProxy::GenericUpstream> inner_;
};

class TcpPool : public TcpProxy::GenericConnPool, public TcpProxy::GenericConnectionPoolCallbacks {
public:
  TcpPool(TcpProxy::GenericConnPoolPtr inner, Network::Connection& downstream,
          StreamInfo::StreamInfo& info, std::shared_ptr<TunnelCallbacks> tunnel = nullptr)
      : tunnel_(std::move(tunnel)), inner_(std::move(inner)), downstream_(downstream), info_(info) {
  }
  ~TcpPool() override {
    if (reject_) {
      reject_->cancel();
    }
  }
  void newStream(TcpProxy::GenericConnectionPoolCallbacks& callbacks) override {
    callbacks_ = &callbacks;
    inner_->newStream(*this);
  }
  void onGenericPoolReady(StreamInfo::StreamInfo* info,
                          std::unique_ptr<TcpProxy::GenericUpstream>&& upstream,
                          Upstream::HostDescriptionConstSharedPtr& host,
                          const Network::ConnectionInfoProvider& address_provider,
                          Ssl::ConnectionInfoConstSharedPtr ssl_info) override {
    if (tunnel_) {
      const auto state = tunnel_->state();
      tunnel_->accepted();
      auto wrapped = std::make_unique<TunnelUpstream>(std::move(upstream), tunnel_);
      // TCP proxy destroys this pool inside the callback. The upstream now owns
      // the callback guard; a close during the handoff must win over acceptance.
      callbacks_->onGenericPoolReady(info, std::move(wrapped), host, address_provider, ssl_info);
      state->accept();
      return;
    }
    callbacks_->onGenericPoolReady(info, std::move(upstream), host, address_provider, ssl_info);
  }
  void onGenericPoolFailure(ConnectionPool::PoolFailureReason reason,
                            absl::string_view failure_reason,
                            Upstream::HostDescriptionConstSharedPtr host) override {
    if (tunnel_) {
      const auto detail = tunnelFailureReason(failure_reason);
      if (permanentConnectRejection(detail)) {
        tunnel_->reject(detail, host);
        return;
      }
      tunnel_->state()->recordAttemptFailure(detail);
    }
    if (!permanentConnectRejection(failure_reason)) {
      callbacks_->onGenericPoolFailure(reason, failure_reason, host);
      return;
    }
    failure_reason_ = std::string(failure_reason);
    recordRejection(info_, failure_reason_, host);
    if (!reject_) {
      reject_ = downstream_.dispatcher().createSchedulableCallback([this]() {
        // Closing can destroy this pool. Keep the detail alive until close returns.
        const std::string reason = failure_reason_;
        downstream_.close(Network::ConnectionCloseType::NoFlush, reason);
      });
    }
    // In particular, do not call TCP proxy's stub HTTP sendLocalReply callback.
    reject_->scheduleCallbackNextIteration();
  }

private:
  const std::shared_ptr<TunnelCallbacks> tunnel_;
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
  if (!inner) {
    return nullptr;
  }
  auto* router = dynamic_cast<Router::RouterFilterInterface*>(context);
  if (router == nullptr) {
    ENVOY_BUG(false, "HBONE HTTP pool requires a router context");
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
  auto state = std::dynamic_pointer_cast<ConnectState>(
      downstream_info.filterState()->getDataSharedMutableGeneric(ConnectState::Key));
  if (config.has_value() && !state) {
    // HBONE clusters can also serve callers that do not enable CONNECT wait.
    return factory->createGenericConnPool(host, cluster, config, context, upstream_callbacks,
                                          decoder_callbacks, downstream_info);
  }
  if (state) {
    const auto* downstream = context == nullptr ? nullptr : context->downstreamConnection();
    if (!downstream) {
      return nullptr;
    }
    const auto* disable_tunneling =
        downstream_info.filterState()->getDataReadOnly<StreamInfo::BoolAccessor>(
            TcpProxy::DisableTunnelingFilterStateKey);
    if (!config.has_value() || config->usePost() ||
        (disable_tunneling != nullptr && disable_tunneling->value())) {
      state->fail("hbone adapter requires HTTP CONNECT tunneling");
      return nullptr;
    }
    auto& connection = const_cast<Network::Connection&>(*downstream);
    auto tunnel = std::make_shared<TunnelCallbacks>(upstream_callbacks, connection, downstream_info,
                                                    std::move(state));
    auto inner = factory->createGenericConnPool(host, cluster, config, context, *tunnel,
                                                decoder_callbacks, downstream_info);
    if (!inner) {
      return nullptr;
    }
    return std::make_unique<TcpPool>(std::move(inner), connection, downstream_info,
                                     std::move(tunnel));
  }
  auto inner = factory->createGenericConnPool(host, cluster, config, context, upstream_callbacks,
                                              decoder_callbacks, downstream_info);
  if (!inner) {
    return nullptr;
  }
  const auto* downstream = context == nullptr ? nullptr : context->downstreamConnection();
  if (downstream == nullptr) {
    ENVOY_BUG(false, "HBONE TCP pool requires a downstream connection");
    return inner;
  }
  // LoadBalancerContext exposes a const view of the TCP proxy's mutable network
  // connection. Closing that connection is the TCP equivalent of a local reply.
  return std::make_unique<TcpPool>(std::move(inner), const_cast<Network::Connection&>(*downstream),
                                   downstream_info);
}

REGISTER_FACTORY(HttpPoolFactory, Router::GenericConnPoolFactory);
REGISTER_FACTORY(TcpPoolFactory, TcpProxy::GenericConnPoolFactory);

} // namespace Hbone
} // namespace Istio
} // namespace Extensions
} // namespace Envoy
