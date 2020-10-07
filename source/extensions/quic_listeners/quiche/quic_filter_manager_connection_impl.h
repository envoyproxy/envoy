#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/network/connection.h"

#include "common/common/empty_string.h"
#include "common/common/logger.h"
#include "common/network/connection_impl_base.h"
#include "common/stream_info/stream_info_impl.h"

#include "extensions/quic_listeners/quiche/envoy_quic_connection.h"
#include "extensions/quic_listeners/quiche/envoy_quic_simulated_watermark_buffer.h"

namespace Envoy {
namespace Quic {

// Act as a Network::Connection to HCM and a FilterManager to FilterFactoryCb.
class QuicFilterManagerConnectionImpl : public Network::ConnectionImplBase {
public:
  QuicFilterManagerConnectionImpl(EnvoyQuicConnection& connection, Event::Dispatcher& dispatcher,
                                  uint32_t send_buffer_limit);

  // Network::FilterManager
  // Overridden to delegate calls to filter_manager_.
  void addWriteFilter(Network::WriteFilterSharedPtr filter) override;
  void addFilter(Network::FilterSharedPtr filter) override;
  void addReadFilter(Network::ReadFilterSharedPtr filter) override;
  bool initializeReadFilters() override;

  // Network::Connection
  void addBytesSentCallback(Network::Connection::BytesSentCb /*cb*/) override {
    // TODO(danzh): implement to support proxy. This interface is only called from
    // TCP proxy code.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  void enableHalfClose(bool enabled) override;
  void close(Network::ConnectionCloseType type) override;
  Event::Dispatcher& dispatcher() override { return dispatcher_; }
  std::string nextProtocol() const override { return EMPTY_STRING; }
  void noDelay(bool /*enable*/) override {
    // No-op. TCP_NODELAY doesn't apply to UDP.
  }
  void readDisable(bool /*disable*/) override { NOT_REACHED_GCOVR_EXCL_LINE; }
  void detectEarlyCloseWhenReadDisabled(bool /*value*/) override { NOT_REACHED_GCOVR_EXCL_LINE; }
  bool readEnabled() const override { return true; }
  const Network::Address::InstanceConstSharedPtr& remoteAddress() const override;
  const Network::Address::InstanceConstSharedPtr& directRemoteAddress() const override;
  const Network::Address::InstanceConstSharedPtr& localAddress() const override;
  absl::optional<Network::Connection::UnixDomainSocketPeerCredentials>
  unixSocketPeerCredentials() const override {
    // Unix domain socket is not supported.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  void setConnectionStats(const Network::Connection::ConnectionStats& stats) override {
    // TODO(danzh): populate stats.
    Network::ConnectionImplBase::setConnectionStats(stats);
    quic_connection_->setConnectionStats(stats);
  }
  Ssl::ConnectionInfoConstSharedPtr ssl() const override;
  Network::Connection::State state() const override {
    if (quic_connection_ != nullptr && quic_connection_->connected()) {
      return Network::Connection::State::Open;
    }
    return Network::Connection::State::Closed;
  }
  void write(Buffer::Instance& /*data*/, bool /*end_stream*/) override {
    // All writes should be handled by Quic internally.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  void setBufferLimits(uint32_t limit) override;
  uint32_t bufferLimit() const override {
    // As quic connection is not HTTP1.1, this method shouldn't be called by HCM.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  bool localAddressRestored() const override {
    // SO_ORIGINAL_DST not supported by QUIC.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  bool aboveHighWatermark() const override;

  const Network::ConnectionSocket::OptionsSharedPtr& socketOptions() const override;
  StreamInfo::StreamInfo& streamInfo() override { return stream_info_; }
  const StreamInfo::StreamInfo& streamInfo() const override { return stream_info_; }
  absl::string_view transportFailureReason() const override { return transport_failure_reason_; }
  absl::optional<std::chrono::milliseconds> lastRoundTripTime() const override { return {}; }

  // Network::FilterManagerConnection
  void rawWrite(Buffer::Instance& data, bool end_stream) override;

  // Network::ReadBufferSource
  Network::StreamBuffer getReadBuffer() override { return {empty_buffer_, false}; }
  // Network::WriteBufferSource
  Network::StreamBuffer getWriteBuffer() override { NOT_REACHED_GCOVR_EXCL_LINE; }

  // Update the book keeping of the aggregated buffered bytes cross all the
  // streams, and run watermark check.
  void adjustBytesToSend(int64_t delta);

  // Called after each write when a previous connection close call is postponed.
  void maybeApplyDelayClosePolicy();

  uint32_t bytesToSend() { return bytes_to_send_; }

protected:
  // Propagate connection close to network_connection_callbacks_.
  void onConnectionCloseEvent(const quic::QuicConnectionCloseFrame& frame,
                              quic::ConnectionCloseSource source);

  void closeConnectionImmediately() override;

  virtual bool hasDataToWrite() PURE;

  EnvoyQuicConnection* quic_connection_{nullptr};

private:
  // Called when aggregated buffered bytes across all the streams exceeds high watermark.
  void onSendBufferHighWatermark();
  // Called when aggregated buffered bytes across all the streams declines to low watermark.
  void onSendBufferLowWatermark();

  // Currently ConnectionManagerImpl is the one and only filter. If more network
  // filters are added, ConnectionManagerImpl should always be the last one.
  // Its onRead() is only called once to trigger ReadFilter::onNewConnection()
  // and the rest incoming data bypasses these filters.
  Network::FilterManagerImpl filter_manager_;

  StreamInfo::StreamInfoImpl stream_info_;
  std::string transport_failure_reason_;
  uint32_t bytes_to_send_{0};
  // Keeps the buffer state of the connection, and react upon the changes of how many bytes are
  // buffered cross all streams' send buffer. The state is evaluated and may be changed upon each
  // stream write. QUICHE doesn't buffer data in connection, all the data is buffered in stream's
  // send buffer.
  EnvoyQuicSimulatedWatermarkBuffer write_buffer_watermark_simulation_;
  Buffer::OwnedImpl empty_buffer_;
};

} // namespace Quic
} // namespace Envoy
