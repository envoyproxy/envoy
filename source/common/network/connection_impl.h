#pragma once

#include <atomic>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/network/transport_socket.h"

#include "common/buffer/watermark_buffer.h"
#include "common/event/libevent.h"
#include "common/network/connection_impl_base.h"
#include "common/stream_info/stream_info_impl.h"

#include "absl/types/optional.h"

namespace Envoy {
class RandomPauseFilter;
class TestPauseFilter;

namespace Network {

/**
 * Utility functions for the connection implementation.
 */
class ConnectionImplUtility {
public:
  /**
   * Update the buffer stats for a connection.
   * @param delta supplies the data read/written.
   * @param new_total supplies the final total buffer size.
   * @param previous_total supplies the previous final total buffer size. previous_total will be
   *        updated to new_total when the call is complete.
   * @param stat_total supplies the counter to increment with the delta.
   * @param stat_current supplies the gauge that should be updated with the delta of previous_total
   *        and new_total.
   */
  static void updateBufferStats(uint64_t delta, uint64_t new_total, uint64_t& previous_total,
                                Stats::Counter& stat_total, Stats::Gauge& stat_current);
};

/**
 * Implementation of Network::Connection and Network::FilterManagerConnection.
 */
class ConnectionImpl : public ConnectionImplBase, public TransportSocketCallbacks {
public:
  ConnectionImpl(Event::Dispatcher& dispatcher, ConnectionSocketPtr&& socket,
                 TransportSocketPtr&& transport_socket, StreamInfo::StreamInfo& stream_info,
                 bool connected);

  ~ConnectionImpl() override;

  // Network::FilterManager
  void addWriteFilter(WriteFilterSharedPtr filter) override;
  void addFilter(FilterSharedPtr filter) override;
  void addReadFilter(ReadFilterSharedPtr filter) override;
  bool initializeReadFilters() override;

  // Network::Connection
  void addBytesSentCallback(BytesSentCb cb) override;
  void enableHalfClose(bool enabled) override;
  void close(ConnectionCloseType type) final;
  std::string nextProtocol() const override { return transport_socket_->protocol(); }
  void noDelay(bool enable) override;
  void readDisable(bool disable) override;
  void detectEarlyCloseWhenReadDisabled(bool value) override { detect_early_close_ = value; }
  bool readEnabled() const override;
  const Address::InstanceConstSharedPtr& remoteAddress() const override {
    return socket_->remoteAddress();
  }
  const Address::InstanceConstSharedPtr& directRemoteAddress() const override {
    return socket_->directRemoteAddress();
  }
  const Address::InstanceConstSharedPtr& localAddress() const override {
    return socket_->localAddress();
  }
  absl::optional<UnixDomainSocketPeerCredentials> unixSocketPeerCredentials() const override;
  Ssl::ConnectionInfoConstSharedPtr ssl() const override { return transport_socket_->ssl(); }
  State state() const override;
  void write(Buffer::Instance& data, bool end_stream) override;
  void setBufferLimits(uint32_t limit) override;
  uint32_t bufferLimit() const override { return read_buffer_limit_; }
  bool localAddressRestored() const override { return socket_->localAddressRestored(); }
  bool aboveHighWatermark() const override { return write_buffer_above_high_watermark_; }
  const ConnectionSocket::OptionsSharedPtr& socketOptions() const override {
    return socket_->options();
  }
  absl::string_view requestedServerName() const override { return socket_->requestedServerName(); }
  StreamInfo::StreamInfo& streamInfo() override { return stream_info_; }
  const StreamInfo::StreamInfo& streamInfo() const override { return stream_info_; }
  absl::string_view transportFailureReason() const override;
  absl::optional<std::chrono::milliseconds> lastRoundTripTime() const override;

  // Network::FilterManagerConnection
  void rawWrite(Buffer::Instance& data, bool end_stream) override;

  // Network::ReadBufferSource
  StreamBuffer getReadBuffer() override { return {read_buffer_, read_end_stream_}; }
  // Network::WriteBufferSource
  StreamBuffer getWriteBuffer() override {
    return {*current_write_buffer_, current_write_end_stream_};
  }

  // Network::TransportSocketCallbacks
  IoHandle& ioHandle() final { return socket_->ioHandle(); }
  const IoHandle& ioHandle() const override { return socket_->ioHandle(); }
  Connection& connection() override { return *this; }
  void raiseEvent(ConnectionEvent event) override;
  // Should the read buffer be drained?
  bool shouldDrainReadBuffer() override {
    return read_buffer_limit_ > 0 && read_buffer_.length() >= read_buffer_limit_;
  }
  // Mark read buffer ready to read in the event loop. This is used when yielding following
  // shouldDrainReadBuffer().
  // TODO(htuch): While this is the basis for also yielding to other connections to provide some
  // fair sharing of CPU resources, the underlying event loop does not make any fairness guarantees.
  // Reconsider how to make fairness happen.
  void setReadBufferReady() override { ioHandle().activateFileEvents(Event::FileReadyType::Read); }
  void flushWriteBuffer() override;

  // Obtain global next connection ID. This should only be used in tests.
  static uint64_t nextGlobalIdForTest() { return next_global_id_; }

protected:
  // A convenience function which returns true if
  // 1) The read disable count is zero or
  // 2) The read disable count is one due to the read buffer being overrun.
  // In either case the consumer of the data would like to read from the buffer.
  // If the read count is greater than one, or equal to one when the buffer is
  // not overrun, then the consumer of the data has called readDisable, and does
  // not want to read.
  bool consumerWantsToRead();

  // Network::ConnectionImplBase
  void closeConnectionImmediately() final;

  void closeSocket(ConnectionEvent close_type);

  void onReadBufferLowWatermark();
  void onReadBufferHighWatermark();
  void onWriteBufferLowWatermark();
  void onWriteBufferHighWatermark();

  TransportSocketPtr transport_socket_;
  ConnectionSocketPtr socket_;
  StreamInfo::StreamInfo& stream_info_;
  FilterManagerImpl filter_manager_;

  // Ensure that if the consumer of the data from this connection isn't
  // consuming, that the connection eventually stops reading from the wire.
  Buffer::WatermarkBuffer read_buffer_;
  // This must be a WatermarkBuffer, but as it is created by a factory the ConnectionImpl only has
  // a generic pointer.
  // It MUST be defined after the filter_manager_ as some filters may have callbacks that
  // write_buffer_ invokes during its clean up.
  Buffer::InstancePtr write_buffer_;
  uint32_t read_buffer_limit_ = 0;
  bool connecting_{false};
  ConnectionEvent immediate_error_event_{ConnectionEvent::Connected};
  bool bind_error_{false};

private:
  friend class Envoy::RandomPauseFilter;
  friend class Envoy::TestPauseFilter;

  void onFileEvent(uint32_t events);
  void onRead(uint64_t read_buffer_size);
  void onReadReady();
  void onWriteReady();
  void updateReadBufferStats(uint64_t num_read, uint64_t new_size);
  void updateWriteBufferStats(uint64_t num_written, uint64_t new_size);

  // Write data to the connection bypassing filter chain (optionally).
  void write(Buffer::Instance& data, bool end_stream, bool through_filter_chain);

  // Returns true iff end of stream has been both written and read.
  bool bothSidesHalfClosed();

  static std::atomic<uint64_t> next_global_id_;

  std::list<BytesSentCb> bytes_sent_callbacks_;
  // Tracks the number of times reads have been disabled. If N different components call
  // readDisabled(true) this allows the connection to only resume reads when readDisabled(false)
  // has been called N times.
  uint64_t last_read_buffer_size_{};
  uint64_t last_write_buffer_size_{};
  Buffer::Instance* current_write_buffer_{};
  uint32_t read_disable_count_{0};
  bool write_buffer_above_high_watermark_ : 1;
  bool detect_early_close_ : 1;
  bool enable_half_close_ : 1;
  bool read_end_stream_raised_ : 1;
  bool read_end_stream_ : 1;
  bool write_end_stream_ : 1;
  bool current_write_end_stream_ : 1;
  bool dispatch_buffered_data_ : 1;
};

class ServerConnectionImpl : public ConnectionImpl, virtual public ServerConnection {
public:
  ServerConnectionImpl(Event::Dispatcher& dispatcher, ConnectionSocketPtr&& socket,
                       TransportSocketPtr&& transport_socket, StreamInfo::StreamInfo& stream_info,
                       bool connected);

  // ServerConnection impl
  void setTransportSocketConnectTimeout(std::chrono::milliseconds timeout) override;
  void raiseEvent(ConnectionEvent event) override;

private:
  void onTransportSocketConnectTimeout();

  bool transport_connect_pending_{true};
  // Implements a timeout for the transport socket signaling connection. The timer is enabled by a
  // call to setTransportSocketConnectTimeout and is reset when the connection is established.
  Event::TimerPtr transport_socket_connect_timer_;
};

/**
 * libevent implementation of Network::ClientConnection.
 */
class ClientConnectionImpl : public ConnectionImpl, virtual public ClientConnection {
public:
  ClientConnectionImpl(Event::Dispatcher& dispatcher,
                       const Address::InstanceConstSharedPtr& remote_address,
                       const Address::InstanceConstSharedPtr& source_address,
                       Network::TransportSocketPtr&& transport_socket,
                       const Network::ConnectionSocket::OptionsSharedPtr& options);

  // Network::ClientConnection
  void connect() override;

private:
  StreamInfo::StreamInfoImpl stream_info_;
};

} // namespace Network
} // namespace Envoy
