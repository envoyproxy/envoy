#include "extensions/quic_listeners/quiche/quic_filter_manager_connection_impl.h"

#include <memory>

namespace Envoy {
namespace Quic {

QuicFilterManagerConnectionImpl::QuicFilterManagerConnectionImpl(
    std::unique_ptr<EnvoyQuicConnection> connection, Event::Dispatcher& dispatcher)
    : quic_connection_(std::move(connection)), filter_manager_(*this), dispatcher_(dispatcher),
      stream_info_(dispatcher.timeSource()) {
  // TODO(danzh): Use QUIC specific enum value.
  stream_info_.protocol(Http::Protocol::Http2);
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

void QuicFilterManagerConnectionImpl::addConnectionCallbacks(Network::ConnectionCallbacks& cb) {
  network_connection_callbacks_.push_back(&cb);
}

void QuicFilterManagerConnectionImpl::addBytesSentCallback(
    Network::Connection::BytesSentCb /*cb*/) {
  // TODO(danzh): implement to support proxy. This interface is only called from
  // TCP proxy code.
  ASSERT(false, "addBytesSentCallback is not implemented for QUIC");
}

void QuicFilterManagerConnectionImpl::enableHalfClose(bool enabled) {
  ASSERT(!enabled, "Quic connection doesn't support half close.");
}

void QuicFilterManagerConnectionImpl::setBufferLimits(uint32_t /*limit*/) {
  // TODO(danzh): add interface to quic for connection level buffer throttling.
  // Currently read buffer is capped by connection level flow control. And
  // write buffer is not capped.
  ENVOY_CONN_LOG(error, "Quic manages its own buffer currently.", *this);
}

uint32_t QuicFilterManagerConnectionImpl::bufferLimit() const {
  // As quic connection is not HTTP1.1, this method shouldn't be called by HCM.
  NOT_REACHED_GCOVR_EXCL_LINE;
}

void QuicFilterManagerConnectionImpl::close(Network::ConnectionCloseType type) {
  if (type != Network::ConnectionCloseType::NoFlush) {
    // TODO(danzh): Implement FlushWrite and FlushWriteAndDelay mode.
    ENVOY_CONN_LOG(error, "Flush write is not implemented for QUIC.", *this);
  }
  quic_connection_->CloseConnection(quic::QUIC_NO_ERROR, "Closed by application",
                                    quic::ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}

void QuicFilterManagerConnectionImpl::setDelayedCloseTimeout(std::chrono::milliseconds timeout) {
  ASSERT(timeout == std::chrono::milliseconds::zero(),
         "Delayed close of connection is not supported");
}

std::chrono::milliseconds QuicFilterManagerConnectionImpl::delayedCloseTimeout() const {
  // Not called outside of Network::ConnectionImpl. Maybe remove this interface
  // from Network::Connection.
  NOT_REACHED_GCOVR_EXCL_LINE;
}

const Network::ConnectionSocket::OptionsSharedPtr&
QuicFilterManagerConnectionImpl::socketOptions() const {
  ENVOY_CONN_LOG(
      error,
      "QUIC connection socket is merely a wrapper, and doesn't have any specific socket options.",
      *this);
  return quic_connection_->connectionSocket()->options();
}

const Network::Address::InstanceConstSharedPtr&
QuicFilterManagerConnectionImpl::remoteAddress() const {
  ASSERT(quic_connection_->connectionSocket() != nullptr,
         "remoteAddress() should only be called after OnPacketHeader");
  return quic_connection_->connectionSocket()->remoteAddress();
}

const Network::Address::InstanceConstSharedPtr&
QuicFilterManagerConnectionImpl::localAddress() const {
  ASSERT(quic_connection_->connectionSocket() != nullptr,
         "localAddress() should only be called after OnPacketHeader");
  return quic_connection_->connectionSocket()->localAddress();
}

Ssl::ConnectionInfoConstSharedPtr QuicFilterManagerConnectionImpl::ssl() const {
  // TODO(danzh): construct Ssl::ConnectionInfo from crypto stream
  ENVOY_CONN_LOG(error, "Ssl::ConnectionInfo instance is not populated.", *this);
  return nullptr;
}

void QuicFilterManagerConnectionImpl::rawWrite(Buffer::Instance& /*data*/, bool /*end_stream*/) {
  // Network filter should stop iteration.
  NOT_REACHED_GCOVR_EXCL_LINE;
}

void QuicFilterManagerConnectionImpl::onConnectionCloseEvent(
    const quic::QuicConnectionCloseFrame& frame, quic::ConnectionCloseSource source) {
  // Tell network callbacks about connection close.
  raiseEvent(source == quic::ConnectionCloseSource::FROM_PEER
                 ? Network::ConnectionEvent::RemoteClose
                 : Network::ConnectionEvent::LocalClose);
  transport_failure_reason_ = absl::StrCat(quic::QuicErrorCodeToString(frame.quic_error_code),
                                           " with details: ", frame.error_details);
}

void QuicFilterManagerConnectionImpl::raiseEvent(Network::ConnectionEvent event) {
  for (auto callback : network_connection_callbacks_) {
    callback->onEvent(event);
  }
}

} // namespace Quic
} // namespace Envoy
