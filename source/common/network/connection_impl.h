#pragma once

#include <atomic>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"

#include "common/buffer/watermark_buffer.h"
#include "common/common/logger.h"
#include "common/event/libevent.h"
#include "common/network/filter_manager_impl.h"
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
class ConnectionImpl : public FilterManagerConnection,
                       public TransportSocketCallbacks,
                       protected Logger::Loggable<Logger::Id::connection> {
public:
  ConnectionImpl(Event::Dispatcher& dispatcher, ConnectionSocketPtr&& socket,
                 TransportSocketPtr&& transport_socket, bool connected);

  ~ConnectionImpl();

  // Network::FilterManager
  void addWriteFilter(WriteFilterSharedPtr filter) override;
  void addFilter(FilterSharedPtr filter) override;
  void addReadFilter(ReadFilterSharedPtr filter) override;
  bool initializeReadFilters() override;

  // Network::Connection
  void addConnectionCallbacks(ConnectionCallbacks& cb) override;
  void addBytesSentCallback(BytesSentCb cb) override;
  void enableHalfClose(bool enabled) override;
  void close(ConnectionCloseType type) override;
  Event::Dispatcher& dispatcher() override;
  uint64_t id() const override;
  std::string nextProtocol() const override { return transport_socket_->protocol(); }
  void noDelay(bool enable) override;
  void readDisable(bool disable) override;
  void detectEarlyCloseWhenReadDisabled(bool value) override { detect_early_close_ = value; }
  bool readEnabled() const override;
  const Address::InstanceConstSharedPtr& remoteAddress() const override {
    return socket_->remoteAddress();
  }
  const Address::InstanceConstSharedPtr& localAddress() const override {
    return socket_->localAddress();
  }
  absl::optional<UnixDomainSocketPeerCredentials> unixSocketPeerCredentials() const override;
  void setConnectionStats(const ConnectionStats& stats) override;
  const Ssl::ConnectionInfo* ssl() const override { return transport_socket_->ssl(); }
  State state() const override;
  void write(Buffer::Instance& data, bool end_stream) override;
  void setBufferLimits(uint32_t limit) override;
  uint32_t bufferLimit() const override { return read_buffer_limit_; }
  bool localAddressRestored() const override { return socket_->localAddressRestored(); }
  bool aboveHighWatermark() const override { return above_high_watermark_; }
  const ConnectionSocket::OptionsSharedPtr& socketOptions() const override {
    return socket_->options();
  }
  absl::string_view requestedServerName() const override { return socket_->requestedServerName(); }
  StreamInfo::StreamInfo& streamInfo() override { return stream_info_; }
  const StreamInfo::StreamInfo& streamInfo() const override { return stream_info_; }
  absl::string_view transportFailureReason() const override;

  // Network::FilterManagerConnection
  void rawWrite(Buffer::Instance& data, bool end_stream) override;

  // Network::ReadBufferSource
  StreamBuffer getReadBuffer() override { return {read_buffer_, read_end_stream_}; }
  // Network::WriteBufferSource
  StreamBuffer getWriteBuffer() override {
    return {*current_write_buffer_, current_write_end_stream_};
  }

  // Network::TransportSocketCallbacks
  IoHandle& ioHandle() override { return socket_->ioHandle(); }
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
  void setReadBufferReady() override { file_event_->activate(Event::FileReadyType::Read); }

  // Obtain global next connection ID. This should only be used in tests.
  static uint64_t nextGlobalIdForTest() { return next_global_id_; }

  void setDelayedCloseTimeout(std::chrono::milliseconds timeout) override {
    // Validate that this is only called prior to issuing a close() or closeSocket().
    ASSERT(delayed_close_timer_ == nullptr && ioHandle().isOpen());
    delayed_close_timeout_ = timeout;
  }
  std::chrono::milliseconds delayedCloseTimeout() const override { return delayed_close_timeout_; }

protected:
  virtual void closeSocket(ConnectionEvent close_type);

  void initializeConnection(bool connected);

  void onLowWatermark();
  void onHighWatermark();

  TransportSocketPtr transport_socket_;
  ConnectionSocketPtr socket_;
  FilterManagerImpl filter_manager_;
  StreamInfo::StreamInfoImpl stream_info_;

  Buffer::OwnedImpl read_buffer_;
  // This must be a WatermarkBuffer, but as it is created by a factory the ConnectionImpl only has
  // a generic pointer.
  Buffer::InstancePtr write_buffer_;
  uint32_t read_buffer_limit_ = 0;
  std::chrono::milliseconds delayed_close_timeout_{0};

protected:
  bool connecting_{false};
  ConnectionEvent immediate_error_event_{ConnectionEvent::Connected};
  bool bind_error_{false};
  Event::FileEventPtr file_event_;

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

  // Callback issued when a delayed close timeout triggers.
  void onDelayedCloseTimeout();

  void initializeDelayedCloseTimer();
  bool inDelayedClose() const { return delayed_close_state_ != DelayedCloseState::None; }

  static std::atomic<uint64_t> next_global_id_;

  // States associated with delayed closing of the connection (i.e., when the underlying socket is
  // not immediately close()d as a result of a ConnectionImpl::close()).
  enum class DelayedCloseState {
    None,
    // The socket will be closed immediately after the buffer is flushed _or_ if a period of
    // inactivity after the last write event greater than or equal to delayed_close_timeout_ has
    // elapsed.
    CloseAfterFlush,
    // The socket will be closed after a grace period of delayed_close_timeout_ has elapsed after
    // the socket is flushed _or_ if a period of inactivity after the last write event greater than
    // or equal to delayed_close_timeout_ has elapsed.
    CloseAfterFlushAndWait
  };
  DelayedCloseState delayed_close_state_{DelayedCloseState::None};

  Event::Dispatcher& dispatcher_;
  const uint64_t id_;
  Event::TimerPtr delayed_close_timer_;
  std::list<ConnectionCallbacks*> callbacks_;
  std::list<BytesSentCb> bytes_sent_callbacks_;
  bool read_enabled_{true};
  bool above_high_watermark_{false};
  bool detect_early_close_{true};
  bool enable_half_close_{false};
  bool read_end_stream_raised_{false};
  bool read_end_stream_{false};
  bool write_end_stream_{false};
  bool current_write_end_stream_{false};
  Buffer::Instance* current_write_buffer_{};
  uint64_t last_read_buffer_size_{};
  uint64_t last_write_buffer_size_{};
  std::unique_ptr<ConnectionStats> connection_stats_;
  // Tracks the number of times reads have been disabled. If N different components call
  // readDisabled(true) this allows the connection to only resume reads when readDisabled(false)
  // has been called N times.
  uint32_t read_disable_count_{0};
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

  void closeSocket(ConnectionEvent close_type) override;
  void initializeClientConnection();

private:
  Address::InstanceConstSharedPtr remote_address_;
  Address::InstanceConstSharedPtr source_address_;
  Network::ConnectionSocket::OptionsSharedPtr options_;
};

} // namespace Network
} // namespace Envoy
