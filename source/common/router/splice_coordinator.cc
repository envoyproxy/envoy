#include "source/common/router/splice_coordinator.h"

#include "envoy/http/header_map.h"
#include "envoy/network/address.h"
#include "envoy/stats/scope.h"

#include "source/common/common/assert.h"
#include "source/common/router/router.h"
#include "source/common/router/upstream_request.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Router {

namespace {

// Returns the OS fd that backs the splice.
absl::optional<os_fd_t> spliceableFd(Network::Connection& connection) {
  if (connection.state() != Network::Connection::State::Open) {
    return absl::nullopt;
  }
  // An internal-listener connection has no kernel fd to splice on.
  if (connection.connectionInfoProvider().localAddress()->type() ==
      Network::Address::Type::EnvoyInternal) {
    return absl::nullopt;
  }
  const os_fd_t fd = connection.getSocket()->ioHandle().fdDoNotUse();
  if (fd == INVALID_SOCKET) {
    return absl::nullopt;
  }
  return fd;
}

} // namespace

SpliceCoordinator::SpliceCoordinator(UpstreamRequest& upstream_request)
    : upstream_request_(upstream_request) {}

SpliceCoordinator::~SpliceCoordinator() = default;

bool SpliceCoordinator::maybeArmForResponse(const Http::ResponseHeaderMap& headers,
                                            bool end_stream) {
  if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.http1_ktls_body_splice")) {
    return false;
  }
  // A header-only response has no body to splice.
  if (end_stream) {
    return false;
  }
  // The request encoder must be done before the upstream connection can be borrowed.
  if (!upstream_request_.encodeComplete()) {
    return false;
  }
  // The splice needs a fixed body boundary.
  const Http::HeaderEntry* content_length_header = headers.ContentLength();
  if (content_length_header == nullptr) {
    return false;
  }
  uint64_t content_length = 0;
  if (!absl::SimpleAtoi(content_length_header->value().getStringView(), &content_length) ||
      content_length < MinSpliceBodyBytes) {
    return false;
  }
  // Both legs must be single, non-multiplexed HTTP/1.1 sockets.
  OptRef<Network::Connection> upstream = upstreamConnection();
  OptRef<Network::Connection> downstream = downstreamConnection();
  if (!upstream.has_value() || !downstream.has_value()) {
    return false;
  }
  // Upstream kTLS reads yield decrypted plaintext.
  OptRef<const Network::KtlsBytestreamInfo> ktls = upstream->ktlsBytestreamInfo();
  if (!ktls.has_value() || !ktls->installed || !ktls->trusted_peer) {
    return false;
  }
  // Raw writes are safe only to plaintext or installed-kTLS sockets.
  if (!sinkLegIsRawOrKtls(downstream.ref())) {
    return false;
  }

  content_length_ = content_length;
  direction_ = Direction::Download;
  engage_polls_ = 0;
  completion_status_ = TcpProxy::SpliceCompletion::Closed;
  armed_ = true;
  ENVOY_LOG(debug, "kTLS body-splice armed for {}-byte response body", content_length_);
  return true;
}

bool SpliceCoordinator::maybeArmForRequest(const Http::RequestHeaderMap& headers, bool end_stream) {
  if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.http1_ktls_body_splice")) {
    return false;
  }
  // A body-less request has nothing to splice.
  if (end_stream) {
    return false;
  }
  // The splice needs a fixed body boundary.
  const Http::HeaderEntry* content_length_header = headers.ContentLength();
  if (content_length_header == nullptr) {
    return false;
  }
  uint64_t content_length = 0;
  if (!absl::SimpleAtoi(content_length_header->value().getStringView(), &content_length) ||
      content_length < MinSpliceBodyBytes) {
    return false;
  }
  // Raw reads are safe only from plaintext or installed-kTLS sockets.
  OptRef<Network::Connection> downstream = downstreamConnection();
  if (!downstream.has_value() || !sinkLegIsRawOrKtls(downstream.ref())) {
    return false;
  }

  content_length_ = content_length;
  direction_ = Direction::Upload;
  engage_polls_ = 0;
  completion_status_ = TcpProxy::SpliceCompletion::Closed;
  armed_ = true;
  // Hold the request body in the kernel until the upstream installs kTLS-TX.
  downstream->readDisable(true);
  source_read_disabled_ = true;
  ENVOY_LOG(debug, "kTLS body-splice armed for {}-byte request body", content_length_);
  return true;
}

void SpliceCoordinator::scheduleEngage() {
  if (!armed_) {
    return;
  }
  if (engage_callback_ == nullptr) {
    engage_callback_ =
        upstream_request_.parent_.callbacks()->dispatcher().createSchedulableCallback(
            [this]() { engage(); });
  }
  // Engage after this read unwinds and headers are encoded.
  engage_callback_->scheduleCallbackCurrentIteration();
}

bool SpliceCoordinator::bufferPreEngageBody(Buffer::Instance& data, bool end_stream) {
  ASSERT(armed_ && !engaged());
  // Stop holding when there is no body left to splice or the in-memory bound is exceeded.
  if (end_stream || pre_engage_body_.length() + data.length() > MaxHeldBodyBytes) {
    abandon();
    return false;
  }
  // Hold the body back from the sink codec. Engage emits it ahead of the spliced remainder.
  pre_engage_body_.move(data);
  return true;
}

void SpliceCoordinator::disarm() {
  armed_ = false;
  if (engage_callback_ != nullptr) {
    engage_callback_->cancel();
  }
  if (engage_poll_timer_ != nullptr) {
    engage_poll_timer_->disableTimer();
  }
}

void SpliceCoordinator::abandon() {
  // Held bytes must precede resumed reads on the wire.
  incSpliceCounter("abandoned");
  flushPreEngageBody();
  maybeReadEnableSource();
  disarm();
}

bool SpliceCoordinator::sinkLegIsRawOrKtls(Network::Connection& connection) {
  return TcpProxy::spliceLegIsRawOrKtls(connection);
}

void SpliceCoordinator::onSpliceProgress() {
  upstream_request_.resetPerTryIdleTimer();
  // The codec is bypassed, so byte movement refreshes the HCM stream idle timer.
  upstream_request_.parent_.callbacks()->resetIdleTimer();
  // Re-arm the no-progress watchdog.
  if (progress_watchdog_ != nullptr) {
    progress_watchdog_->enableTimer(ProgressWatchdogTimeout);
  }
}

void SpliceCoordinator::armProgressWatchdog() {
  if (progress_watchdog_ == nullptr) {
    progress_watchdog_ = upstream_request_.parent_.callbacks()->dispatcher().createTimer([this]() {
      // Part of the body may have reached the sink, so a stalled splice cannot recover.
      ENVOY_LOG(debug, "kTLS body-splice no-progress watchdog fired, resetting");
      upstream_request_.onResetStream(Http::StreamResetReason::ConnectionTermination,
                                      "kTLS body-splice stalled");
    });
  }
  progress_watchdog_->enableTimer(ProgressWatchdogTimeout);
}

void SpliceCoordinator::incSpliceCounter(absl::string_view event) {
  // Count once per splice decision, never per byte.
  const Upstream::ClusterInfoConstSharedPtr cluster = upstream_request_.parent_.cluster();
  if (cluster == nullptr) {
    return;
  }
  cluster->statsScope().counterFromString(absl::StrCat("http1_ktls_splice.", event)).inc();
}

void SpliceCoordinator::maybeReadEnableSource() {
  if (!source_read_disabled_) {
    return;
  }
  source_read_disabled_ = false;
  OptRef<Network::Connection> downstream = downstreamConnection();
  // readDisable() asserts the connection is open.
  if (downstream.has_value() && downstream->state() == Network::Connection::State::Open) {
    // Reinstall before re-enabling a source leg detached by engage().
    if (legs_detached_) {
      downstream->reinstallFileEvents();
    }
    downstream->readDisable(false);
  }
}

void SpliceCoordinator::rescheduleEngage() {
  if (++engage_polls_ > MaxEngagePolls) {
    // Fall back if upstream kTLS-TX does not become ready.
    ENVOY_LOG(debug, "kTLS body-splice skipped: upstream not ready after {} polls", engage_polls_);
    abandon();
    return;
  }
  if (engage_poll_timer_ == nullptr) {
    engage_poll_timer_ =
        upstream_request_.parent_.callbacks()->dispatcher().createTimer([this]() { engage(); });
  }
  // Space the poll across connect, handshake, and kTLS-TX install I/O.
  engage_poll_timer_->enableTimer(std::chrono::milliseconds(2));
}

void SpliceCoordinator::engage() {
  if (!armed_) {
    return;
  }

  OptRef<Network::Connection> upstream = upstreamConnection();
  OptRef<Network::Connection> downstream = downstreamConnection();

  // Upload waits until the upstream connects and installs kTLS-TX.
  if (direction_ == Direction::Upload) {
    // Shadows need the normal decodeData path to see the request body.
    if (upstream_request_.parent_.shadowStreamsActive()) {
      abandon();
      return;
    }
    if (!downstream.has_value()) {
      abandon();
      return;
    }
    if (!upstream.has_value()) {
      // Once the upstream stream exists, an empty splice connection means it is not HTTP/1.1.
      if (upstream_request_.upstream_ != nullptr) {
        ENVOY_LOG(debug, "kTLS body-splice skipped: upstream is not a borrowable HTTP/1.1 socket");
        abandon();
        return;
      }
      rescheduleEngage(); // Upstream pool not ready yet.
      return;
    }
    OptRef<const Network::KtlsBytestreamInfo> ktls = upstream->ktlsBytestreamInfo();
    if (!ktls.has_value()) {
      // The splice cannot frame TLS records in userspace.
      abandon();
      return;
    }
    if (!ktls->installed || !ktls->trusted_peer) {
      rescheduleEngage(); // kTLS-TX still installing.
      return;
    }
  }

  armed_ = false;

  if (!upstream.has_value() || !downstream.has_value()) {
    ENVOY_LOG(debug, "kTLS body-splice skipped: a leg is no longer borrowable");
    abandon();
    return;
  }

  // Re-validate both legs after the schedulable-callback gap.
  const bool download = direction_ == Direction::Download;
  OptRef<const Network::KtlsBytestreamInfo> ktls = upstream->ktlsBytestreamInfo();
  if (!ktls.has_value() || !ktls->installed || !ktls->trusted_peer ||
      !sinkLegIsRawOrKtls(downstream.ref())) {
    abandon();
    return;
  }
  // The sink is downstream for downloads and upstream for uploads.
  Network::Connection& sink = download ? downstream.ref() : upstream.ref();
  const absl::optional<os_fd_t> up_fd = spliceableFd(upstream.ref());
  const absl::optional<os_fd_t> down_fd = spliceableFd(downstream.ref());
  if (!up_fd.has_value() || !down_fd.has_value()) {
    abandon();
    return;
  }
  // If the whole body arrived before engage, deliver it normally.
  const uint64_t buffered = pre_engage_body_.length();
  if (buffered >= content_length_) {
    abandon();
    return;
  }
  spliced_body_bytes_ = content_length_ - buffered;

  auto pump = splice_pump_factory_(
      down_fd.value(), up_fd.value(), /*up_is_ktls=*/true,
      upstream_request_.parent_.callbacks()->dispatcher(),
      [this](TcpProxy::SpliceCompletion status) { onSpliceComplete(status); },
      // Byte movement refreshes liveness timers while the codec is bypassed.
      [this](uint64_t) { onSpliceProgress(); }, [this](uint64_t) { onSpliceProgress(); });

  // Create pipes before draining the sink write buffer so failure can still fall back.
  if (!pump->createPipes(/*need_u2d=*/download, /*need_d2u=*/!download)) {
    ENVOY_LOG(debug, "kTLS body-splice skipped: pipe creation failed, using buffered path");
    abandon();
    return;
  }

  // Preserve wire order by emitting headers and held body before the spliced remainder.
  std::string pre_engage = sink.extractPendingWriteForSplice();
  const size_t pending_size = pre_engage.size();
  pre_engage.resize(pending_size + buffered);
  pre_engage_body_.copyOut(0, buffered, pre_engage.data() + pending_size);
  pre_engage_body_.drain(buffered);
  // The sink write buffer is now drained, so the splice is committed.
  incSpliceCounter("engaged");
  // prepare() can fail only after bytes were drained, so failure resets the stream.
  const bool ok = download ? pump->prepare(std::move(pre_engage), /*initial_d2u=*/"")
                           : pump->prepare(/*initial_u2d=*/"", std::move(pre_engage));
  if (!ok) {
    // Pending output was already drained, so this is a reset, not a fallback.
    incSpliceCounter("truncated");
    ENVOY_LOG(warn, "kTLS body-splice setup failed after taking pending output, resetting");
    upstream_request_.onResetStream(Http::StreamResetReason::ConnectionTermination,
                                    "kTLS body-splice setup failed");
    return;
  }
  if (download) {
    pump->setBounds(/*u2d_limit=*/spliced_body_bytes_, /*d2u_limit=*/absl::nullopt);
  } else {
    pump->setBounds(/*u2d_limit=*/absl::nullopt, /*d2u_limit=*/spliced_body_bytes_);
  }

  // Upload retries would replay only the buffered prefix after engage.
  if (!download) {
    upstream_request_.parent_.disableRetries();
  }

  // The pump owns both fd registrations until finalize or reset.
  upstream->getSocket()->ioHandle().resetFileEvents();
  downstream->getSocket()->ioHandle().resetFileEvents();
  legs_detached_ = true;
  upstream_connection_ = upstream;
  downstream_connection_ = downstream;
  splice_pump_ = std::move(pump);
  splice_pump_->arm();
  // Reap a stalled splice even while the codec is detached.
  armProgressWatchdog();
  ENVOY_LOG(debug, "kTLS body-splice engaged: {} {} body bytes (down_fd={}, up_fd={})",
            spliced_body_bytes_, download ? "response" : "request", down_fd.value(), up_fd.value());
}

void SpliceCoordinator::flushPreEngageBody() {
  if (pre_engage_body_.length() == 0) {
    return;
  }
  // Forward held body so the normal path can resume.
  if (direction_ == Direction::Download) {
    // Response body to the downstream encoder.
    upstream_request_.parent_.onUpstreamData(pre_engage_body_, upstream_request_, false);
  } else {
    // Request body to the upstream encoder via the upstream filter chain.
    upstream_request_.filter_manager_->decodeData(pre_engage_body_, false);
  }
}

void SpliceCoordinator::onSpliceComplete(TcpProxy::SpliceCompletion status) {
  // Defer teardown until the pump callback unwinds.
  completion_status_ = status;
  if (finalize_callback_ == nullptr) {
    finalize_callback_ =
        upstream_request_.parent_.callbacks()->dispatcher().createSchedulableCallback(
            [this]() { finalize(); });
  }
  finalize_callback_->scheduleCallbackCurrentIteration();
}

void SpliceCoordinator::finalize() {
  // Destroy the pump first so its FileEvents are gone. Keep ConnectionImpl events detached while
  // completeSpliced* finalizes the codec stream, then re-arm each reusable leg.
  if (progress_watchdog_ != nullptr) {
    progress_watchdog_->disableTimer();
  }
  splice_pump_.reset();

  // completeSpliced* may delete this coordinator, so capture the legs before calling it.
  OptRef<Network::Connection> up = upstream_connection_;
  OptRef<Network::Connection> down = downstream_connection_;
  upstream_connection_ = {};
  downstream_connection_ = {};

  if (completion_status_ != TcpProxy::SpliceCompletion::BoundsReached) {
    // A truncated splice cannot recover because part of the body may have reached the sink.
    incSpliceCounter("truncated");
    ENVOY_LOG(debug, "kTLS body-splice aborted before the Content-Length boundary, resetting");
    maybeReadEnableSource();
    // The upstream NoFlush close cascades into downstream file-event code. Reinstall the download
    // sink first so that cascade never sees a detached file event.
    if (direction_ == Direction::Download && legs_detached_ && down.has_value() &&
        down->state() == Network::Connection::State::Open) {
      down->reinstallFileEvents();
    }
    if (up.has_value() && up->state() == Network::Connection::State::Open) {
      up->close(Network::ConnectionCloseType::NoFlush);
    }
    return;
  }

  // Count before completeSpliced* can delete this coordinator.
  incSpliceCounter("completed");

  // The sink codec did not see the body, so account its full Content-Length here.
  StreamInfo::StreamInfo& downstream_info = upstream_request_.parent_.callbacks()->streamInfo();
  ENVOY_LOG(debug, "kTLS body-splice complete: {} body bytes relayed", spliced_body_bytes_);
  if (direction_ == Direction::Download) {
    if (downstream_info.getDownstreamBytesMeter() != nullptr) {
      downstream_info.getDownstreamBytesMeter()->addWireBytesSent(content_length_);
    }
    downstream_info.addBytesSent(content_length_);
    upstream_request_.stream_info_.addBytesReceived(spliced_body_bytes_);
    // Re-arm downstream before completing the response.
    if (down.has_value() && down->state() == Network::Connection::State::Open) {
      down->reinstallFileEvents();
    }
    // completeSplicedResponse may delete this coordinator.
    ASSERT(upstream_request_.upstream_ != nullptr);
    upstream_request_.upstream_->completeSplicedResponse(spliced_body_bytes_);
    // Re-arm upstream only after the codec stream is finalized.
    if (up.has_value() && up->state() == Network::Connection::State::Open) {
      up->reinstallFileEvents();
    }
    return;
  }

  // Upload reuses both legs and then awaits the response.
  if (up.has_value() && up->state() == Network::Connection::State::Open) {
    up->reinstallFileEvents();
  }
  maybeReadEnableSource();
  if (downstream_info.getUpstreamBytesMeter() != nullptr) {
    downstream_info.getUpstreamBytesMeter()->addWireBytesSent(content_length_);
  }
  downstream_info.addBytesReceived(spliced_body_bytes_);
  upstream_request_.stream_info_.addBytesSent(content_length_);
  // completeSplicedRequest may delete this coordinator.
  auto downstream_callbacks = upstream_request_.parent_.callbacks()->downstreamCallbacks();
  if (downstream_callbacks.has_value()) {
    downstream_callbacks->completeSplicedRequest(spliced_body_bytes_);
  }
}

void SpliceCoordinator::reset() {
  armed_ = false;
  if (engage_callback_ != nullptr) {
    engage_callback_->cancel();
  }
  if (engage_poll_timer_ != nullptr) {
    engage_poll_timer_->disableTimer();
  }
  if (finalize_callback_ != nullptr) {
    finalize_callback_->cancel();
  }
  if (progress_watchdog_ != nullptr) {
    progress_watchdog_->disableTimer();
  }
  // reset() runs from UpstreamRequest teardown, not from inside a pump callback.
  if (splice_pump_ != nullptr) {
    // Count an in-flight splice once when teardown beats finalize().
    incSpliceCounter(completion_status_ == TcpProxy::SpliceCompletion::BoundsReached ? "completed"
                                                                                     : "truncated");
  }
  splice_pump_.reset();
  maybeReadEnableSource();
  // Match finalize's truncation ordering when teardown wins the race.
  if (direction_ == Direction::Download && legs_detached_ && downstream_connection_.has_value() &&
      downstream_connection_->state() == Network::Connection::State::Open) {
    downstream_connection_->reinstallFileEvents();
  }
  if (upstream_connection_.has_value() &&
      upstream_connection_->state() == Network::Connection::State::Open) {
    // NoFlush close re-enters UpstreamRequest::onResetStream inline.
    upstream_request_.on_reset_stream_in_progress_ = true;
    upstream_connection_->close(Network::ConnectionCloseType::NoFlush);
  }
  upstream_connection_ = {};
  downstream_connection_ = {};
  legs_detached_ = false;
}

OptRef<Network::Connection> SpliceCoordinator::upstreamConnection() {
  return upstream_request_.upstream_ != nullptr
             ? upstream_request_.upstream_->upstreamConnectionForSplice()
             : OptRef<Network::Connection>{};
}

OptRef<Network::Connection> SpliceCoordinator::downstreamConnection() {
  auto downstream_callbacks = upstream_request_.parent_.callbacks()->downstreamCallbacks();
  return downstream_callbacks.has_value() ? downstream_callbacks->downstreamConnectionForSplice()
                                          : OptRef<Network::Connection>{};
}

} // namespace Router
} // namespace Envoy
