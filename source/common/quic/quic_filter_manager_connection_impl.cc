#include "source/common/quic/quic_filter_manager_connection_impl.h"

#include <initializer_list>
#include <memory>

#include "source/common/quic/quic_ssl_connection_info.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Quic {

QuicFilterManagerConnectionImpl::QuicFilterManagerConnectionImpl(
    QuicNetworkConnection& connection, const quic::QuicConnectionId& connection_id,
    Event::Dispatcher& dispatcher, uint32_t send_buffer_limit,
    std::shared_ptr<QuicSslConnectionInfo>&& info,
    std::unique_ptr<StreamInfo::StreamInfo>&& stream_info)
    // Using this for purpose other than logging is not safe. Because QUIC connection id can be
    // 18 bytes, so there might be collision when it's hashed to 8 bytes.
    : Network::ConnectionImplBase(dispatcher, /*id=*/connection_id.Hash()),
      network_connection_(&connection), quic_ssl_info_(std::move(info)),
      filter_manager_(
          std::make_unique<Network::FilterManagerImpl>(*this, *connection.connectionSocket())),
      stream_info_(std::move(stream_info)),
      write_buffer_watermark_simulation_(
          send_buffer_limit / 2, send_buffer_limit, [this]() { onSendBufferLowWatermark(); },
          [this]() { onSendBufferHighWatermark(); }, ENVOY_LOGGER()) {
  stream_info_->protocol(Http::Protocol::Http3);
  network_connection_->connectionSocket()->connectionInfoProvider().setSslConnection(
      Ssl::ConnectionInfoConstSharedPtr(quic_ssl_info_));
  fix_quic_lifetime_issues_ =
      Runtime::runtimeFeatureEnabled("envoy.reloadable_features.quic_fix_filter_manager_uaf");
}

void QuicFilterManagerConnectionImpl::addWriteFilter(Network::WriteFilterSharedPtr filter) {
  filter_manager_->addWriteFilter(filter);
}

void QuicFilterManagerConnectionImpl::addFilter(Network::FilterSharedPtr filter) {
  filter_manager_->addFilter(filter);
}

void QuicFilterManagerConnectionImpl::addReadFilter(Network::ReadFilterSharedPtr filter) {
  filter_manager_->addReadFilter(filter);
}

void QuicFilterManagerConnectionImpl::removeReadFilter(Network::ReadFilterSharedPtr filter) {
  filter_manager_->removeReadFilter(filter);
}

bool QuicFilterManagerConnectionImpl::initializeReadFilters() {
  return filter_manager_->initializeReadFilters();
}

void QuicFilterManagerConnectionImpl::enableHalfClose(bool enabled) {
  RELEASE_ASSERT(!enabled, "Quic connection doesn't support half close.");
}

bool QuicFilterManagerConnectionImpl::isHalfCloseEnabled() const {
  // Quic doesn't support half close.
  return false;
}

void QuicFilterManagerConnectionImpl::setBufferLimits(uint32_t /*limit*/) {
  // Currently read buffer is capped by connection level flow control. And write buffer limit is set
  // during construction. Changing the buffer limit during the life time of the connection is not
  // supported.
  IS_ENVOY_BUG("unexpected call to setBufferLimits");
}

bool QuicFilterManagerConnectionImpl::aboveHighWatermark() const {
  return write_buffer_watermark_simulation_.isAboveHighWatermark();
}

void QuicFilterManagerConnectionImpl::close(Network::ConnectionCloseType type) {
  if (network_connection_ == nullptr) {
    // Already detached from quic connection.
    return;
  }
  if (!initialized_) {
    // Delay close till the first ProcessUdpPacket() call.
    close_type_during_initialize_ = type;
    return;
  }
  const bool delayed_close_timeout_configured = delayed_close_timeout_.count() > 0;
  if (hasDataToWrite() && type != Network::ConnectionCloseType::NoFlush) {
    if (delayed_close_timeout_configured) {
      // QUIC connection has unsent data and caller wants to flush them. Wait for flushing or
      // timeout.
      if (!inDelayedClose()) {
        // Only set alarm if not in delay close mode yet.
        initializeDelayedCloseTimer();
      }
      // Update delay close state according to current call.
      if (type == Network::ConnectionCloseType::FlushWriteAndDelay) {
        delayed_close_state_ = DelayedCloseState::CloseAfterFlushAndWait;
      } else {
        ASSERT(type == Network::ConnectionCloseType::FlushWrite);
        delayed_close_state_ = DelayedCloseState::CloseAfterFlush;
      }
    } else {
      delayed_close_state_ = DelayedCloseState::CloseAfterFlush;
    }
  } else if (hasDataToWrite()) {
    // Quic connection has unsent data but caller wants to close right away.
    ASSERT(type == Network::ConnectionCloseType::NoFlush);
    closeConnectionImmediately();
  } else {
    // Quic connection doesn't have unsent data. It's up to the caller and
    // the configuration whether to wait or not before closing.
    if (delayed_close_timeout_configured &&
        type == Network::ConnectionCloseType::FlushWriteAndDelay) {
      if (!inDelayedClose()) {
        initializeDelayedCloseTimer();
      }
      delayed_close_state_ = DelayedCloseState::CloseAfterFlushAndWait;
    } else {
      closeConnectionImmediately();
    }
  }
}

const Network::ConnectionSocket::OptionsSharedPtr&
QuicFilterManagerConnectionImpl::socketOptions() const {
  return network_connection_->connectionSocket()->options();
}

Ssl::ConnectionInfoConstSharedPtr QuicFilterManagerConnectionImpl::ssl() const {
  return {quic_ssl_info_};
}

void QuicFilterManagerConnectionImpl::rawWrite(Buffer::Instance& /*data*/, bool /*end_stream*/) {
  // Network filter should stop iteration.
  IS_ENVOY_BUG("unexpected call to rawWrite");
}

void QuicFilterManagerConnectionImpl::updateBytesBuffered(uint64_t old_buffered_bytes,
                                                          uint64_t new_buffered_bytes) {
  int64_t delta = new_buffered_bytes - old_buffered_bytes;
  const uint64_t bytes_to_send_old = bytes_to_send_;
  bytes_to_send_ += delta;
  if (delta < 0) {
    ENVOY_BUG(bytes_to_send_old > bytes_to_send_, "Underflowed");
  } else {
    ENVOY_BUG(bytes_to_send_old <= bytes_to_send_, "Overflowed");
  }
  write_buffer_watermark_simulation_.checkHighWatermark(bytes_to_send_);
  write_buffer_watermark_simulation_.checkLowWatermark(bytes_to_send_);
}

void QuicFilterManagerConnectionImpl::maybeUpdateDelayCloseTimer(bool has_sent_any_data) {
  if (!inDelayedClose()) {
    ASSERT(!close_type_during_initialize_.has_value());
    return;
  }
  if (has_sent_any_data && delayed_close_timer_ != nullptr) {
    // Re-arm delay close timer if at least some buffered data is flushed.
    delayed_close_timer_->enableTimer(delayed_close_timeout_);
  }
}

void QuicFilterManagerConnectionImpl::onWriteEventDone() { maybeApplyDelayedClose(); }

void QuicFilterManagerConnectionImpl::maybeApplyDelayedClose() {
  if (!hasDataToWrite() && inDelayedClose() &&
      delayed_close_state_ != DelayedCloseState::CloseAfterFlushAndWait) {
    closeConnectionImmediately();
  }
}

void QuicFilterManagerConnectionImpl::onConnectionCloseEvent(
    const quic::QuicConnectionCloseFrame& frame, quic::ConnectionCloseSource source,
    const quic::ParsedQuicVersion& version) {
  transport_failure_reason_ = absl::StrCat(quic::QuicErrorCodeToString(frame.quic_error_code),
                                           " with details: ", frame.error_details);
  if (network_connection_ != nullptr) {
    // Tell network callbacks about connection close if not detached yet.
    raiseConnectionEvent(source == quic::ConnectionCloseSource::FROM_PEER
                             ? Network::ConnectionEvent::RemoteClose
                             : Network::ConnectionEvent::LocalClose);
    ASSERT(network_connection_ != nullptr);
    network_connection_ = nullptr;
  }

  if (!fix_quic_lifetime_issues_) {
    filter_manager_ = nullptr;
  }
  if (!codec_stats_.has_value()) {
    // The connection was closed before it could be used. Stats are not recorded.
    return;
  }
  switch (version.transport_version) {
  case quic::QUIC_VERSION_IETF_DRAFT_29:
    codec_stats_->quic_version_h3_29_.inc();
    return;
  case quic::QUIC_VERSION_IETF_RFC_V1:
    codec_stats_->quic_version_rfc_v1_.inc();
    return;
  default:
    IS_ENVOY_BUG(fmt::format("Unexpected QUIC version {}",
                             quic::QuicVersionToString(version.transport_version)));
  }
}

void QuicFilterManagerConnectionImpl::closeConnectionImmediately() {
  if (quicConnection() == nullptr) {
    return;
  }
  quicConnection()->CloseConnection(
      quic::QUIC_NO_ERROR,
      (localCloseReason().empty()
           ? "Closed by application"
           : absl::StrCat("Closed by application with reason: ", localCloseReason())),
      quic::ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}

void QuicFilterManagerConnectionImpl::onSendBufferHighWatermark() {
  ENVOY_CONN_LOG(trace, "onSendBufferHighWatermark", *this);
  for (auto callback : callbacks_) {
    callback->onAboveWriteBufferHighWatermark();
  }
}

void QuicFilterManagerConnectionImpl::onSendBufferLowWatermark() {
  ENVOY_CONN_LOG(trace, "onSendBufferLowWatermark", *this);
  for (auto callback : callbacks_) {
    callback->onBelowWriteBufferLowWatermark();
  }
}

absl::optional<std::chrono::milliseconds>
QuicFilterManagerConnectionImpl::lastRoundTripTime() const {
  if (quicConnection() == nullptr) {
    return {};
  }

  const auto* rtt_stats = quicConnection()->sent_packet_manager().GetRttStats();
  if (!rtt_stats->latest_rtt().IsZero()) {
    return std::chrono::milliseconds(rtt_stats->latest_rtt().ToMilliseconds());
  }

  return std::chrono::milliseconds(rtt_stats->initial_rtt().ToMilliseconds());
}

void QuicFilterManagerConnectionImpl::configureInitialCongestionWindow(
    uint64_t bandwidth_bits_per_sec, std::chrono::microseconds rtt) {
  if (quicConnection() != nullptr) {
    quic::SendAlgorithmInterface::NetworkParams params(
        quic::QuicBandwidth::FromBitsPerSecond(bandwidth_bits_per_sec),
        quic::QuicTime::Delta::FromMicroseconds(rtt.count()),
        /*allow_cwnd_to_decrease=*/false);
    // NOTE: Different QUIC congestion controllers implement this method differently, for example,
    // the cubic implementation does not respect |params.allow_cwnd_to_decrease|. Check the
    // implementations for the exact behavior.
    quicConnection()->AdjustNetworkParameters(params);
  }
}

absl::optional<uint64_t> QuicFilterManagerConnectionImpl::congestionWindowInBytes() const {
  if (quicConnection() == nullptr) {
    return {};
  }

  uint64_t cwnd = quicConnection()->sent_packet_manager().GetCongestionWindowInBytes();
  if (cwnd == 0) {
    return {};
  }

  return cwnd;
}

void QuicFilterManagerConnectionImpl::maybeHandleCloseDuringInitialize() {
  if (close_type_during_initialize_.has_value()) {
    close(close_type_during_initialize_.value());
    close_type_during_initialize_ = absl::nullopt;
  }
}

} // namespace Quic
} // namespace Envoy
