#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/common/optref.h"
#include "envoy/event/schedulable_cb.h"
#include "envoy/event/timer.h"
#include "envoy/http/header_map.h"
#include "envoy/network/connection.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/tcp_proxy/splice_pump.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Router {

class UpstreamRequest;

// Coordinates one HTTP/1.1 Content-Length body splice for an UpstreamRequest. Downloads relay from
// a kTLS upstream to the downstream. Uploads relay from downstream to a kTLS-TX upstream. The pump
// bypasses userspace buffers and filters, so the runtime guard must only be enabled when no filter
// mutates the body. Clean completion stops at Content-Length and keeps both sockets reusable.
// Errors reset the stream and do not reuse the sockets.
class SpliceCoordinator : public Logger::Loggable<Logger::Id::router> {
public:
  explicit SpliceCoordinator(UpstreamRequest& upstream_request);
  ~SpliceCoordinator();

  // Direction of the body relayed by the splice.
  enum class Direction {
    Download, // Upstream (kTLS) to downstream, a response body.
    Upload,   // Downstream to upstream (kTLS-TX), a request body.
  };

  // Arms a download splice for the decoded response when eligible.
  bool maybeArmForResponse(const Http::ResponseHeaderMap& headers, bool end_stream);

  // Arms an upload splice for the request when eligible.
  bool maybeArmForRequest(const Http::RequestHeaderMap& headers, bool end_stream);

  // Schedules engage after headers are encoded onto the sink leg.
  void scheduleEngage();

  // Holds body that arrives before engage so it can precede the spliced remainder.
  bool bufferPreEngageBody(Buffer::Instance& data, bool end_stream);

  // Cancels a pending arm and tears down any in-flight splice.
  void reset();

  // Direction-specific armed predicates.
  bool armedForResponse() const { return armed_ && direction_ == Direction::Download; }
  bool armedForRequest() const { return armed_ && direction_ == Direction::Upload; }
  bool engaged() const { return splice_pump_ != nullptr; }

  // Builds the pump at engage time.
  using SplicePumpFactory = std::function<TcpProxy::SplicePumpPtr(
      os_fd_t down_fd, os_fd_t up_fd, bool up_is_ktls, Event::Dispatcher& dispatcher,
      TcpProxy::SplicePump::CompletionCb on_complete, TcpProxy::SplicePump::BytesCb on_u2d_bytes,
      TcpProxy::SplicePump::BytesCb on_d2u_bytes)>;
  void setSplicePumpFactoryForTest(SplicePumpFactory factory) {
    splice_pump_factory_ = std::move(factory);
  }

private:
  // Minimum Content-Length worth splicing.
  static constexpr uint64_t MinSpliceBodyBytes = 64 * 1024;
  // Upper bound on body held in memory while waiting to engage.
  static constexpr uint64_t MaxHeldBodyBytes = 4 * 1024 * 1024;
  // Defensive bound on upload engage polling.
  static constexpr uint32_t MaxEngagePolls = 64;

  void engage();
  void onSpliceComplete(TcpProxy::SpliceCompletion status);
  void finalize();
  void disarm();
  // Gives up an armed splice before it engages.
  void abandon();
  // Reschedules engage while upload waits for upstream kTLS-TX.
  void rescheduleEngage();
  // Forwards held body back through the normal path.
  void flushPreEngageBody();
  // Re-enables upload source reads after they were disabled at arm.
  void maybeReadEnableSource();
  // Resolves the single HTTP/1.1 legs borrowed by the splice.
  OptRef<Network::Connection> upstreamConnection();
  OptRef<Network::Connection> downstreamConnection();
  // True if the leg can safely send or receive raw spliced bytes.
  static bool sinkLegIsRawOrKtls(Network::Connection& connection);
  // Refreshes liveness timers after byte movement.
  void onSpliceProgress();
  void armProgressWatchdog();
  // Increments the per-cluster splice lifecycle counter.
  void incSpliceCounter(absl::string_view event);

  UpstreamRequest& upstream_request_;
  Event::SchedulableCallbackPtr engage_callback_;
  Event::TimerPtr engage_poll_timer_;
  Event::SchedulableCallbackPtr finalize_callback_;
  // Reaps a splice that stalls with zero byte movement.
  Event::TimerPtr progress_watchdog_;
  TcpProxy::SplicePumpPtr splice_pump_;
  // Constructs the real pump unless a test installs a deterministic double.
  SplicePumpFactory splice_pump_factory_{
      [](os_fd_t down_fd, os_fd_t up_fd, bool up_is_ktls, Event::Dispatcher& dispatcher,
         TcpProxy::SplicePump::CompletionCb on_complete, TcpProxy::SplicePump::BytesCb on_u2d_bytes,
         TcpProxy::SplicePump::BytesCb on_d2u_bytes) {
        return std::make_unique<TcpProxy::SplicePump>(
            down_fd, up_fd, up_is_ktls, dispatcher, std::move(on_complete), std::move(on_u2d_bytes),
            std::move(on_d2u_bytes));
      }};
  // Borrowed legs held until finalize re-arms their file events.
  OptRef<Network::Connection> upstream_connection_;
  OptRef<Network::Connection> downstream_connection_;
  uint64_t content_length_{0};
  // Body held before engage so it can precede the spliced remainder.
  Buffer::OwnedImpl pre_engage_body_;
  // Bytes the in-kernel splice itself relayed.
  uint64_t spliced_body_bytes_{0};
  // No-progress watchdog duration.
  static constexpr std::chrono::seconds ProgressWatchdogTimeout{30};
  TcpProxy::SpliceCompletion completion_status_{TcpProxy::SpliceCompletion::Closed};
  Direction direction_{Direction::Download};
  uint32_t engage_polls_{0};
  bool armed_{false};
  // True when upload source reads are disabled until engage.
  bool source_read_disabled_{false};
  // True once engage detached both ConnectionImpl file events.
  bool legs_detached_{false};
};

using SpliceCoordinatorPtr = std::unique_ptr<SpliceCoordinator>;

} // namespace Router
} // namespace Envoy
