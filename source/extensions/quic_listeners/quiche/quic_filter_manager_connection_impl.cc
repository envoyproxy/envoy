#include "extensions/quic_listeners/quiche/quic_filter_manager_connection_impl.h"

#include <memory>

namespace Envoy {
namespace Quic {

QuicFilterManagerConnectionImpl::QuicFilterManagerConnectionImpl(EnvoyQuicConnection& connection,
                                                                 Event::Dispatcher& dispatcher,
                                                                 uint32_t send_buffer_limit)
    // Using this for purpose other than logging is not safe. Because QUIC connection id can be
    // 18 bytes, so there might be collision when it's hashed to 8 bytes.
    : Network::ConnectionImplBase(dispatcher, /*id=*/connection.connection_id().Hash()),
      quic_connection_(&connection), filter_manager_(*this), stream_info_(dispatcher.timeSource()),
      write_buffer_watermark_simulation_(
          send_buffer_limit / 2, send_buffer_limit, [this]() { onSendBufferLowWatermark(); },
          [this]() { onSendBufferHighWatermark(); }, ENVOY_LOGGER()) {
  stream_info_.protocol(Http::Protocol::Http3);
}

void QuicFilterManagerConnectionImpl::addWriteFilter(Network::WriteFilterSharedPtr filter) {
  filter_manager_.addWriteFilter(filter);
}

void QuicFilterManagerConnectionImpl::addFilter(Network::FilterSharedPtr filter) {
  filter_manager_.addFilter(filter);
}

void QuicFilterManagerConnectionImpl::addReadFilter(Network::ReadFilterSharedPtr filter) {
  filter_manager_.addReadFilter(filter);
}

bool QuicFilterManagerConnectionImpl::initializeReadFilters() {
  return filter_manager_.initializeReadFilters();
}

void QuicFilterManagerConnectionImpl::enableHalfClose(bool enabled) {
  RELEASE_ASSERT(!enabled, "Quic connection doesn't support half close.");
}

void QuicFilterManagerConnectionImpl::setBufferLimits(uint32_t /*limit*/) {
  // Currently read buffer is capped by connection level flow control. And write buffer limit is set
  // during construction. Changing the buffer limit during the life time of the connection is not
  // supported.
  NOT_REACHED_GCOVR_EXCL_LINE;
}

bool QuicFilterManagerConnectionImpl::aboveHighWatermark() const {
  return write_buffer_watermark_simulation_.isAboveHighWatermark();
}

void QuicFilterManagerConnectionImpl::close(Network::ConnectionCloseType type) {
  if (quic_connection_ == nullptr) {
    // Already detached from quic connection.
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
    quic_connection_->OnCanWrite();
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
  return quic_connection_->connectionSocket()->options();
}

const Network::Address::InstanceConstSharedPtr&
QuicFilterManagerConnectionImpl::remoteAddress() const {
  ASSERT(quic_connection_->connectionSocket() != nullptr,
         "remoteAddress() should only be called after OnPacketHeader");
  return quic_connection_->connectionSocket()->remoteAddress();
}

const Network::Address::InstanceConstSharedPtr&
QuicFilterManagerConnectionImpl::directRemoteAddress() const {
  ASSERT(quic_connection_->connectionSocket() != nullptr,
         "directRemoteAddress() should only be called after OnPacketHeader");
  return quic_connection_->connectionSocket()->directRemoteAddress();
}

const Network::Address::InstanceConstSharedPtr&
QuicFilterManagerConnectionImpl::localAddress() const {
  ASSERT(quic_connection_->connectionSocket() != nullptr,
         "localAddress() should only be called after OnPacketHeader");
  return quic_connection_->connectionSocket()->localAddress();
}

Ssl::ConnectionInfoConstSharedPtr QuicFilterManagerConnectionImpl::ssl() const {
  // TODO(danzh): construct Ssl::ConnectionInfo from crypto stream
  return nullptr;
}

void QuicFilterManagerConnectionImpl::rawWrite(Buffer::Instance& /*data*/, bool /*end_stream*/) {
  // Network filter should stop iteration.
  NOT_REACHED_GCOVR_EXCL_LINE;
}

void QuicFilterManagerConnectionImpl::adjustBytesToSend(int64_t delta) {
  bytes_to_send_ += delta;
  write_buffer_watermark_simulation_.checkHighWatermark(bytes_to_send_);
  write_buffer_watermark_simulation_.checkLowWatermark(bytes_to_send_);
}

void QuicFilterManagerConnectionImpl::maybeApplyDelayClosePolicy() {
  if (!inDelayedClose()) {
    return;
  }
  if (hasDataToWrite() || delayed_close_state_ == DelayedCloseState::CloseAfterFlushAndWait) {
    if (delayed_close_timer_ != nullptr) {
      // Re-arm delay close timer on every write event if there are still data
      // buffered or the connection close is supposed to be delayed.
      delayed_close_timer_->enableTimer(delayed_close_timeout_);
    }
  } else {
    closeConnectionImmediately();
  }
}

void QuicFilterManagerConnectionImpl::onConnectionCloseEvent(
    const quic::QuicConnectionCloseFrame& frame, quic::ConnectionCloseSource source) {
  transport_failure_reason_ = absl::StrCat(quic::QuicErrorCodeToString(frame.quic_error_code),
                                           " with details: ", frame.error_details);
  if (quic_connection_ != nullptr) {
    // Tell network callbacks about connection close if not detached yet.
    raiseConnectionEvent(source == quic::ConnectionCloseSource::FROM_PEER
                             ? Network::ConnectionEvent::RemoteClose
                             : Network::ConnectionEvent::LocalClose);
  }
}

void QuicFilterManagerConnectionImpl::closeConnectionImmediately() {
  if (quic_connection_ == nullptr) {
    return;
  }
  quic_connection_->CloseConnection(quic::QUIC_NO_ERROR, "Closed by application",
                                    quic::ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
  quic_connection_ = nullptr;
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

} // namespace Quic
} // namespace Envoy
