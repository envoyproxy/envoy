#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/stream_info/filter_state.h"

#include "source/extensions/transport_sockets/internal_upstream/internal_upstream.h"

namespace Envoy {
namespace Extensions {
namespace Istio {
namespace Hbone {

// Only adapter-generated failures use this marker across connection pool boundaries.
// Ordinary transport errors and application response codes are never classified here.
std::optional<uint32_t> permanentConnectRejection(absl::string_view reason);
std::string tunnelFailureReason(absl::string_view reason);

// Shared only across one internal socket pair, on its worker thread. This object
// must not be propagated into the outer HTTP/2 pool or affect its pool key.
class ConnectState : public StreamInfo::FilterState::Object {
public:
  enum class Phase { Pending, Accepted, Connected, Failed };
  static constexpr absl::string_view Key = "io.istio.hbone.connect_state";

  void accept();
  void recordAttemptFailure(absl::string_view reason);
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
                 StreamInfo::FilterState::Objects objects,
                 Network::TransportSocketPtr inner_socket = nullptr);
  ~UpstreamSocket() override;

  // Network::TransportSocket
  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override;
  Api::SysCallIntResult connect(Network::ConnectionSocket& socket) override;
  void onConnected() override;
  Network::IoResult doRead(Buffer::Instance& buffer) override;
  Network::IoResult doWrite(Buffer::Instance& buffer, bool end_stream) override;
  bool canFlushClose() override;
  absl::string_view failureReason() const override;
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

} // namespace Hbone
} // namespace Istio
} // namespace Extensions
} // namespace Envoy
