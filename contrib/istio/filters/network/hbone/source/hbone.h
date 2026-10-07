#pragma once

#include <functional>
#include <memory>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/tcp_proxy/tcp_proxy.h"
#include "source/extensions/transport_sockets/internal_upstream/internal_upstream.h"

namespace Envoy {
namespace Extensions {
namespace Istio {
namespace Hbone {

// Shared only across one internal socket pair, on its worker thread. This object
// must not be propagated into the outer HTTP/2 pool or affect its pool key.
class ConnectState : public StreamInfo::FilterState::Object {
public:
  enum class Phase { Pending, Accepted, Connected, Failed };
  static constexpr absl::string_view Key = "io.istio.hbone.connect_state";

  void accept();
  void fail(absl::string_view reason);
  void connected();
  void setCallback(std::function<void()> callback) { callback_ = std::move(callback); }
  Phase phase() const { return phase_; }
  absl::string_view failureReason() const { return failure_reason_; }

private:
  void notify();
  Phase phase_{Phase::Pending};
  std::string failure_reason_;
  std::function<void()> callback_;
};

using ConnectStateSharedPtr = std::shared_ptr<ConnectState>;

class UpstreamSocket : public TransportSockets::InternalUpstream::InternalSocket {
public:
  UpstreamSocket(ConnectStateSharedPtr state,
                 std::unique_ptr<envoy::config::core::v3::Metadata> metadata,
                 StreamInfo::FilterState::Objects objects);
  ~UpstreamSocket() override;

  // Network::TransportSocket
  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override;
  Api::SysCallIntResult connect(Network::ConnectionSocket& socket) override;
  void onConnected() override;
  Network::IoResult doRead(Buffer::Instance& buffer) override;
  Network::IoResult doWrite(Buffer::Instance& buffer, bool end_stream) override;
  bool canFlushClose() override;
  absl::string_view failureReason() const override { return state_->failureReason(); }
  void closeSocket(Network::ConnectionEvent event, bool abort_reset) override;

private:
  void scheduleReady();
  void completeConnect();
  void cancel();

  const ConnectStateSharedPtr state_;
  Network::TransportSocketCallbacks* callbacks_{};
  Event::SchedulableCallbackPtr ready_callback_;
  bool internal_connected_{false};
};

// Reuses TCP proxy's HTTP/2 CONNECT pool and stream forwarding. Readiness is
// signaled by the tunnel-level callback, not the outer HTTP pool's callback.
class Filter : public TcpProxy::Filter, public Network::ConnectionCallbacks {
public:
  using TcpProxy::Filter::Filter;
  ~Filter() override;

  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;
  Network::FilterStatus onNewConnection() override;
  void onGenericPoolReady(StreamInfo::StreamInfo* info,
                          std::unique_ptr<TcpProxy::GenericUpstream>&& upstream,
                          Upstream::HostDescriptionConstSharedPtr& host,
                          const Network::ConnectionInfoProvider& address_provider,
                          Ssl::ConnectionInfoConstSharedPtr ssl_info) override;
  void onGenericPoolFailure(ConnectionPool::PoolFailureReason reason,
                            absl::string_view failure_reason,
                            Upstream::HostDescriptionConstSharedPtr host) override;

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

protected:
  void onInitFailure(UpstreamFailureReason reason) override;

private:
  class ConnectCallbacks : public TcpProxy::Filter::UpstreamCallbacks {
  public:
    ConnectCallbacks(Filter* parent, ConnectStateSharedPtr state)
        : TcpProxy::Filter::UpstreamCallbacks(parent), state_(std::move(state)) {}
    void onEvent(Network::ConnectionEvent event) override;

  private:
    const ConnectStateSharedPtr state_;
  };

  ConnectStateSharedPtr state_;
  std::string failure_reason_;
};

} // namespace Hbone
} // namespace Istio
} // namespace Extensions
} // namespace Envoy
