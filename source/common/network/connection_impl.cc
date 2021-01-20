#include "common/network/connection_impl.h"

#include <atomic>
#include <cstdint>
#include <memory>

#include "envoy/common/exception.h"
#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/event/scaled_range_timer_manager.h"
#include "envoy/event/timer.h"
#include "envoy/network/filter.h"
#include "envoy/network/socket.h"

#include "common/common/assert.h"
#include "common/common/dump_state_utils.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/common/scope_tracker.h"
#include "common/network/address_impl.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/raw_buffer_socket.h"
#include "common/network/utility.h"
#include "common/runtime/runtime_features.h"

namespace Envoy {
namespace Network {
namespace {

constexpr absl::string_view kTransportSocketConnectTimeoutTerminationDetails =
    "transport socket timeout was reached";

std::ostream& operator<<(std::ostream& os, Connection::State connection_state) {
  switch (connection_state) {
  case Connection::State::Open:
    return os << "Open";
  case Connection::State::Closing:
    return os << "Closing";
  case Connection::State::Closed:
    return os << "Closed";
  }
  return os;
}

} // namespace

void ConnectionImplUtility::updateBufferStats(uint64_t delta, uint64_t new_total,
                                              uint64_t& previous_total, Stats::Counter& stat_total,
                                              Stats::Gauge& stat_current) {
  if (delta) {
    stat_total.add(delta);
  }

  if (new_total != previous_total) {
    if (new_total > previous_total) {
      stat_current.add(new_total - previous_total);
    } else {
      stat_current.sub(previous_total - new_total);
    }

    previous_total = new_total;
  }
}

std::atomic<uint64_t> ConnectionImpl::next_global_id_;

ConnectionImpl::ConnectionImpl(Event::Dispatcher& dispatcher, ConnectionSocketPtr&& socket,
                               TransportSocketPtr&& transport_socket,
                               StreamInfo::StreamInfo& stream_info, bool connected)
    : ConnectionImplBase(dispatcher, next_global_id_++),
      transport_socket_(std::move(transport_socket)), socket_(std::move(socket)),
      stream_info_(stream_info), filter_manager_(*this),
      write_buffer_(dispatcher.getWatermarkFactory().create(
          [this]() -> void { this->onWriteBufferLowWatermark(); },
          [this]() -> void { this->onWriteBufferHighWatermark(); },
          []() -> void { /* TODO(adisuissa): Handle overflow watermark */ })),
      read_buffer_(dispatcher.getWatermarkFactory().create(
          [this]() -> void { this->onReadBufferLowWatermark(); },
          [this]() -> void { this->onReadBufferHighWatermark(); },
          []() -> void { /* TODO(adisuissa): Handle overflow watermark */ })),
      write_buffer_above_high_watermark_(false), detect_early_close_(true),
      enable_half_close_(false), read_end_stream_raised_(false), read_end_stream_(false),
      write_end_stream_(false), current_write_end_stream_(false), dispatch_buffered_data_(false),
      transport_wants_read_(false) {

  if (!connected) {
    connecting_ = true;
  }

  Event::FileTriggerType trigger = Event::PlatformDefaultTriggerType;

  // We never ask for both early close and read at the same time. If we are reading, we want to
  // consume all available data.
  socket_->ioHandle().initializeFileEvent(
      dispatcher_, [this](uint32_t events) -> void { onFileEvent(events); }, trigger,
      Event::FileReadyType::Read | Event::FileReadyType::Write);

  transport_socket_->setTransportSocketCallbacks(*this);
}

ConnectionImpl::~ConnectionImpl() {
  ASSERT(!ioHandle().isOpen() && delayed_close_timer_ == nullptr,
         "ConnectionImpl was unexpectedly torn down without being closed.");

  // In general we assume that owning code has called close() previously to the destructor being
  // run. This generally must be done so that callbacks run in the correct context (vs. deferred
  // deletion). Hence the assert above. However, call close() here just to be completely sure that
  // the fd is closed and make it more likely that we crash from a bad close callback.
  close(ConnectionCloseType::NoFlush);
}

void ConnectionImpl::addWriteFilter(WriteFilterSharedPtr filter) {
  filter_manager_.addWriteFilter(filter);
}

void ConnectionImpl::addFilter(FilterSharedPtr filter) { filter_manager_.addFilter(filter); }

void ConnectionImpl::addReadFilter(ReadFilterSharedPtr filter) {
  filter_manager_.addReadFilter(filter);
}

void ConnectionImpl::removeReadFilter(ReadFilterSharedPtr filter) {
  filter_manager_.removeReadFilter(filter);
}

bool ConnectionImpl::initializeReadFilters() { return filter_manager_.initializeReadFilters(); }

void ConnectionImpl::close(ConnectionCloseType type) {
  if (!ioHandle().isOpen()) {
    return;
  }

  uint64_t data_to_write = write_buffer_->length();
  ENVOY_CONN_LOG(debug, "closing data_to_write={} type={}", *this, data_to_write, enumToInt(type));
  const bool delayed_close_timeout_set = delayed_close_timeout_.count() > 0;
  if (data_to_write == 0 || type == ConnectionCloseType::NoFlush ||
      !transport_socket_->canFlushClose()) {
    if (data_to_write > 0) {
      // We aren't going to wait to flush, but try to write as much as we can if there is pending
      // data.
      transport_socket_->doWrite(*write_buffer_, true);
    }

    if (type == ConnectionCloseType::FlushWriteAndDelay && delayed_close_timeout_set) {
      // The socket is being closed and either there is no more data to write or the data can not be
      // flushed (!transport_socket_->canFlushClose()). Since a delayed close has been requested,
      // start the delayed close timer if it hasn't been done already by a previous close().
      // NOTE: Even though the delayed_close_state_ is being set to CloseAfterFlushAndWait, since
      // a write event is not being registered for the socket, this logic is simply setting the
      // timer and waiting for it to trigger to close the socket.
      if (!inDelayedClose()) {
        initializeDelayedCloseTimer();
        delayed_close_state_ = DelayedCloseState::CloseAfterFlushAndWait;
        // Monitor for the peer closing the connection.
        ioHandle().enableFileEvents(enable_half_close_ ? 0 : Event::FileReadyType::Closed);
      }
    } else {
      closeConnectionImmediately();
    }
  } else {
    ASSERT(type == ConnectionCloseType::FlushWrite ||
           type == ConnectionCloseType::FlushWriteAndDelay);

    // If there is a pending delayed close, simply update the delayed close state.
    //
    // An example of this condition manifests when a downstream connection is closed early by Envoy,
    // such as when a route can't be matched:
    //   In ConnectionManagerImpl::onData()
    //     1) Via codec_->dispatch(), a local reply with a 404 is sent to the client
    //       a) ConnectionManagerImpl::doEndStream() issues the first connection close() via
    //          ConnectionManagerImpl::checkForDeferredClose()
    //     2) A second close is issued by a subsequent call to
    //        ConnectionManagerImpl::checkForDeferredClose() prior to returning from onData()
    if (inDelayedClose()) {
      // Validate that a delayed close timer is already enabled unless it was disabled via
      // configuration.
      ASSERT(!delayed_close_timeout_set || delayed_close_timer_ != nullptr);
      if (type == ConnectionCloseType::FlushWrite || !delayed_close_timeout_set) {
        delayed_close_state_ = DelayedCloseState::CloseAfterFlush;
      } else {
        delayed_close_state_ = DelayedCloseState::CloseAfterFlushAndWait;
      }
      return;
    }

    // NOTE: At this point, it's already been validated that the connection is not already in
    // delayed close processing and therefore the timer has not yet been created.
    if (delayed_close_timeout_set) {
      initializeDelayedCloseTimer();
      delayed_close_state_ = (type == ConnectionCloseType::FlushWrite)
                                 ? DelayedCloseState::CloseAfterFlush
                                 : DelayedCloseState::CloseAfterFlushAndWait;
    } else {
      delayed_close_state_ = DelayedCloseState::CloseAfterFlush;
    }

    ioHandle().enableFileEvents(Event::FileReadyType::Write |
                                (enable_half_close_ ? 0 : Event::FileReadyType::Closed));
  }
}

Connection::State ConnectionImpl::state() const {
  if (!ioHandle().isOpen()) {
    return State::Closed;
  } else if (inDelayedClose()) {
    return State::Closing;
  } else {
    return State::Open;
  }
}

void ConnectionImpl::closeConnectionImmediately() { closeSocket(ConnectionEvent::LocalClose); }

void ConnectionImpl::setTransportSocketIsReadable() {
  // Remember that the transport requested read resumption, in case the resumption event is not
  // scheduled immediately or is "lost" because read was disabled.
  transport_wants_read_ = true;
  // Only schedule a read activation if the connection is not read disabled to avoid spurious
  // wakeups. When read disabled, the connection will not read from the transport, and limit
  // dispatch to the current contents of the read buffer if its high-watermark is triggered and
  // dispatch_buffered_data_ is set.
  if (read_disable_count_ == 0) {
    ioHandle().activateFileEvents(Event::FileReadyType::Read);
  }
}

bool ConnectionImpl::filterChainWantsData() {
  return read_disable_count_ == 0 ||
         (read_disable_count_ == 1 && read_buffer_->highWatermarkTriggered());
}

void ConnectionImpl::closeSocket(ConnectionEvent close_type) {
  if (!ConnectionImpl::ioHandle().isOpen()) {
    return;
  }

  // No need for a delayed close (if pending) now that the socket is being closed.
  if (delayed_close_timer_) {
    delayed_close_timer_->disableTimer();
    delayed_close_timer_ = nullptr;
  }

  ENVOY_CONN_LOG(debug, "closing socket: {}", *this, static_cast<uint32_t>(close_type));
  transport_socket_->closeSocket(close_type);

  // Drain input and output buffers.
  updateReadBufferStats(0, 0);
  updateWriteBufferStats(0, 0);

  // As the socket closes, drain any remaining data.
  // The data won't be written out at this point, and where there are reference
  // counted buffer fragments, it helps avoid lifetime issues with the
  // connection outlasting the subscriber.
  write_buffer_->drain(write_buffer_->length());

  connection_stats_.reset();

  socket_->close();

  // Call the base class directly as close() is called in the destructor.
  ConnectionImpl::raiseEvent(close_type);
}

void ConnectionImpl::noDelay(bool enable) {
  // There are cases where a connection to localhost can immediately fail (e.g., if the other end
  // does not have enough fds, reaches a backlog limit, etc.). Because we run with deferred error
  // events, the calling code may not yet know that the connection has failed. This is one call
  // where we go outside of libevent and hit the fd directly and this case can fail if the fd is
  // invalid. For this call instead of plumbing through logic that will immediately indicate that a
  // connect failed, we will just ignore the noDelay() call if the socket is invalid since error is
  // going to be raised shortly anyway and it makes the calling code simpler.
  if (!ioHandle().isOpen()) {
    return;
  }

  // Don't set NODELAY for unix domain sockets
  if (socket_->addressType() == Address::Type::Pipe) {
    return;
  }

  // Set NODELAY
  int new_value = enable;
  Api::SysCallIntResult result =
      socket_->setSocketOption(IPPROTO_TCP, TCP_NODELAY, &new_value, sizeof(new_value));
#if defined(__APPLE__)
  if (SOCKET_FAILURE(result.rc_) && result.errno_ == SOCKET_ERROR_INVAL) {
    // Sometimes occurs when the connection is not yet fully formed. Empirically, TCP_NODELAY is
    // enabled despite this result.
    return;
  }
#elif defined(WIN32)
  if (SOCKET_FAILURE(result.rc_) &&
      (result.errno_ == SOCKET_ERROR_AGAIN || result.errno_ == SOCKET_ERROR_INVAL)) {
    // Sometimes occurs when the connection is not yet fully formed. Empirically, TCP_NODELAY is
    // enabled despite this result.
    return;
  }
#endif

  RELEASE_ASSERT(result.rc_ == 0, fmt::format("Failed to set TCP_NODELAY with error {}, {}",
                                              result.errno_, errorDetails(result.errno_)));
}

void ConnectionImpl::onRead(uint64_t read_buffer_size) {
  if (inDelayedClose() || !filterChainWantsData()) {
    return;
  }
  ASSERT(ioHandle().isOpen());

  if (read_buffer_size == 0 && !read_end_stream_) {
    return;
  }

  if (read_end_stream_) {
    // read() on a raw socket will repeatedly return 0 (EOF) once EOF has
    // occurred, so filter out the repeats so that filters don't have
    // to handle repeats.
    //
    // I don't know of any cases where this actually happens (we should stop
    // reading the socket after EOF), but this check guards against any bugs
    // in ConnectionImpl or strangeness in the OS events (epoll, kqueue, etc)
    // and maintains the guarantee for filters.
    if (read_end_stream_raised_) {
      // No further data can be delivered after end_stream
      ASSERT(read_buffer_size == 0);
      return;
    }
    read_end_stream_raised_ = true;
  }

  filter_manager_.onRead();
}

void ConnectionImpl::enableHalfClose(bool enabled) {
  // This code doesn't correctly ensure that EV_CLOSE isn't set if reading is disabled
  // when enabling half-close. This could be fixed, but isn't needed right now, so just
  // ASSERT that it doesn't happen.
  ASSERT(!enabled || read_disable_count_ == 0);

  enable_half_close_ = enabled;
}

void ConnectionImpl::readDisable(bool disable) {
  // Calls to readEnabled on a closed socket are considered to be an error.
  ASSERT(state() == State::Open);

  ENVOY_CONN_LOG(trace, "readDisable: disable={} disable_count={} state={} buffer_length={}", *this,
                 disable, read_disable_count_, static_cast<int>(state()), read_buffer_->length());

  // When we disable reads, we still allow for early close notifications (the equivalent of
  // `EPOLLRDHUP` for an epoll backend). For backends that support it, this allows us to apply
  // back pressure at the kernel layer, but still get timely notification of a FIN. Note that
  // we are not guaranteed to get notified, so even if the remote has closed, we may not know
  // until we try to write. Further note that currently we optionally don't correctly handle half
  // closed TCP connections in the sense that we assume that a remote FIN means the remote intends a
  // full close.
  if (disable) {
    ++read_disable_count_;

    if (state() != State::Open) {
      // If readDisable is called on a closed connection, do not crash.
      return;
    }
    if (read_disable_count_ > 1) {
      // The socket has already been read disabled.
      return;
    }

    // If half-close semantics are enabled, we never want early close notifications; we
    // always want to read all available data, even if the other side has closed.
    if (detect_early_close_ && !enable_half_close_) {
      ioHandle().enableFileEvents(Event::FileReadyType::Write | Event::FileReadyType::Closed);
    } else {
      ioHandle().enableFileEvents(Event::FileReadyType::Write);
    }
  } else {
    ASSERT(read_disable_count_ != 0);
    --read_disable_count_;
    if (state() != State::Open) {
      // If readDisable is called on a closed connection, do not crash.
      return;
    }

    if (read_disable_count_ == 0) {
      // We never ask for both early close and read at the same time. If we are reading, we want to
      // consume all available data.
      ioHandle().enableFileEvents(Event::FileReadyType::Read | Event::FileReadyType::Write);
    }

    if (filterChainWantsData() && (read_buffer_->length() > 0 || transport_wants_read_)) {
      // Sanity check: resumption with read_disable_count_ > 0 should only happen if the read
      // buffer's high watermark has triggered.
      ASSERT(read_buffer_->length() > 0 || read_disable_count_ == 0);

      // If the read_buffer_ is not empty or transport_wants_read_ is true, the connection may be
      // able to process additional bytes even if there is no data in the kernel to kick off the
      // filter chain. Alternately the connection may need read resumption while read disabled and
      // not registered for read events because the read buffer's high-watermark has triggered. To
      // handle these cases, directly schedule a fake read event to make sure the buffered data in
      // the read buffer or in transport socket internal buffers gets processed regardless and
      // ensure that we dispatch it via onRead.
      dispatch_buffered_data_ = true;
      ioHandle().activateFileEvents(Event::FileReadyType::Read);
    }
  }
}

void ConnectionImpl::raiseEvent(ConnectionEvent event) {
  ENVOY_CONN_LOG(trace, "raising connection event {}", *this, event);
  ConnectionImplBase::raiseConnectionEvent(event);
  // We may have pending data in the write buffer on transport handshake
  // completion, which may also have completed in the context of onReadReady(),
  // where no check of the write buffer is made. Provide an opportunity to flush
  // here. If connection write is not ready, this is harmless. We should only do
  // this if we're still open (the above callbacks may have closed).
  if (event == ConnectionEvent::Connected) {
    flushWriteBuffer();
  }
}

bool ConnectionImpl::readEnabled() const {
  // Calls to readEnabled on a closed socket are considered to be an error.
  ASSERT(state() == State::Open);
  return read_disable_count_ == 0;
}

void ConnectionImpl::addBytesSentCallback(BytesSentCb cb) {
  bytes_sent_callbacks_.emplace_back(cb);
}

void ConnectionImpl::rawWrite(Buffer::Instance& data, bool end_stream) {
  write(data, end_stream, false);
}

void ConnectionImpl::write(Buffer::Instance& data, bool end_stream) {
  write(data, end_stream, true);
}

void ConnectionImpl::write(Buffer::Instance& data, bool end_stream, bool through_filter_chain) {
  ASSERT(!end_stream || enable_half_close_);

  if (write_end_stream_) {
    // It is an API violation to write more data after writing end_stream, but a duplicate
    // end_stream with no data is harmless. This catches misuse of the API that could result in data
    // being lost.
    ASSERT(data.length() == 0 && end_stream);

    return;
  }

  if (through_filter_chain) {
    // NOTE: This is kind of a hack, but currently we don't support restart/continue on the write
    //       path, so we just pass around the buffer passed to us in this function. If we ever
    //       support buffer/restart/continue on the write path this needs to get more complicated.
    current_write_buffer_ = &data;
    current_write_end_stream_ = end_stream;
    FilterStatus status = filter_manager_.onWrite();
    current_write_buffer_ = nullptr;

    if (FilterStatus::StopIteration == status) {
      return;
    }
  }

  write_end_stream_ = end_stream;
  if (data.length() > 0 || end_stream) {
    ENVOY_CONN_LOG(trace, "writing {} bytes, end_stream {}", *this, data.length(), end_stream);
    // TODO(mattklein123): All data currently gets moved from the source buffer to the write buffer.
    // This can lead to inefficient behavior if writing a bunch of small chunks. In this case, it
    // would likely be more efficient to copy data below a certain size. VERY IMPORTANT: If this is
    // ever changed, read the comment in SslSocket::doWrite() VERY carefully. That code assumes that
    // we never change existing write_buffer_ chain elements between calls to SSL_write(). That code
    // might need to change if we ever copy here.
    write_buffer_->move(data);

    // Activating a write event before the socket is connected has the side-effect of tricking
    // doWriteReady into thinking the socket is connected. On macOS, the underlying write may fail
    // with a connection error if a call to write(2) occurs before the connection is completed.
    if (!connecting_) {
      ioHandle().activateFileEvents(Event::FileReadyType::Write);
    }
  }
}

void ConnectionImpl::setBufferLimits(uint32_t limit) {
  read_buffer_limit_ = limit;

  // Due to the fact that writes to the connection and flushing data from the connection are done
  // asynchronously, we have the option of either setting the watermarks aggressively, and regularly
  // enabling/disabling reads from the socket, or allowing more data, but then not triggering
  // based on watermarks until 2x the data is buffered in the common case. Given these are all soft
  // limits we err on the side of buffering more triggering watermark callbacks less often.
  //
  // Given the current implementation for straight up TCP proxying, the common case is reading
  // |limit| bytes through the socket, passing |limit| bytes to the connection and the immediately
  // draining |limit| bytes to the socket. Triggering the high watermarks and then immediately
  // triggering the low watermarks would be expensive, but we narrowly avoid triggering high
  // watermark when moving |limit| bytes through the connection because the high watermark
  // computation checks if the size of the buffer exceeds the high watermark value.
  if (limit > 0) {
    write_buffer_->setWatermarks(limit);
    read_buffer_->setWatermarks(limit);
  }
}

void ConnectionImpl::onReadBufferLowWatermark() {
  ENVOY_CONN_LOG(debug, "onBelowReadBufferLowWatermark", *this);
  if (state() == State::Open) {
    readDisable(false);
  }
}

void ConnectionImpl::onReadBufferHighWatermark() {
  ENVOY_CONN_LOG(debug, "onAboveReadBufferHighWatermark", *this);
  if (state() == State::Open) {
    readDisable(true);
  }
}

void ConnectionImpl::onWriteBufferLowWatermark() {
  ENVOY_CONN_LOG(debug, "onBelowWriteBufferLowWatermark", *this);
  ASSERT(write_buffer_above_high_watermark_);
  write_buffer_above_high_watermark_ = false;
  for (ConnectionCallbacks* callback : callbacks_) {
    if (callback) {
      callback->onBelowWriteBufferLowWatermark();
    }
  }
}

void ConnectionImpl::onWriteBufferHighWatermark() {
  ENVOY_CONN_LOG(debug, "onAboveWriteBufferHighWatermark", *this);
  ASSERT(!write_buffer_above_high_watermark_);
  write_buffer_above_high_watermark_ = true;
  for (ConnectionCallbacks* callback : callbacks_) {
    if (callback) {
      callback->onAboveWriteBufferHighWatermark();
    }
  }
}

void ConnectionImpl::onFileEvent(uint32_t events) {
  ScopeTrackerScopeState scope(this, this->dispatcher_);
  ENVOY_CONN_LOG(trace, "socket event: {}", *this, events);

  if (immediate_error_event_ != ConnectionEvent::Connected) {
    if (bind_error_) {
      ENVOY_CONN_LOG(debug, "raising bind error", *this);
      // Update stats here, rather than on bind failure, to give the caller a chance to
      // setConnectionStats.
      if (connection_stats_ && connection_stats_->bind_errors_) {
        connection_stats_->bind_errors_->inc();
      }
    } else {
      ENVOY_CONN_LOG(debug, "raising immediate error", *this);
    }
    closeSocket(immediate_error_event_);
    return;
  }

  if (events & Event::FileReadyType::Closed) {
    // We never ask for both early close and read at the same time. If we are reading, we want to
    // consume all available data.
    ASSERT(!(events & Event::FileReadyType::Read));
    ENVOY_CONN_LOG(debug, "remote early close", *this);
    closeSocket(ConnectionEvent::RemoteClose);
    return;
  }

  if (events & Event::FileReadyType::Write) {
    onWriteReady();
  }

  // It's possible for a write event callback to close the socket (which will cause fd_ to be -1).
  // In this case ignore write event processing.
  if (ioHandle().isOpen() && (events & Event::FileReadyType::Read)) {
    onReadReady();
  }
}

void ConnectionImpl::onReadReady() {
  ENVOY_CONN_LOG(trace, "read ready. dispatch_buffered_data={}", *this, dispatch_buffered_data_);
  const bool latched_dispatch_buffered_data = dispatch_buffered_data_;
  dispatch_buffered_data_ = false;

  ASSERT(!connecting_);

  // We get here while read disabled in two ways.
  // 1) There was a call to setTransportSocketIsReadable(), for example if a raw buffer socket ceded
  //    due to shouldDrainReadBuffer(). In this case we defer the event until the socket is read
  //    enabled.
  // 2) The consumer of connection data called readDisable(true), and instead of reading from the
  //    socket we simply need to dispatch already read data.
  if (read_disable_count_ != 0) {
    // Do not clear transport_wants_read_ when returning early; the early return skips the transport
    // socket doRead call.
    if (latched_dispatch_buffered_data && filterChainWantsData()) {
      onRead(read_buffer_->length());
    }
    return;
  }

  // Clear transport_wants_read_ just before the call to doRead. This is the only way to ensure that
  // the transport socket read resumption happens as requested; onReadReady() returns early without
  // reading from the transport if the read buffer is above high watermark at the start of the
  // method.
  transport_wants_read_ = false;
  IoResult result = transport_socket_->doRead(*read_buffer_);
  uint64_t new_buffer_size = read_buffer_->length();
  updateReadBufferStats(result.bytes_processed_, new_buffer_size);

  // If this connection doesn't have half-close semantics, translate end_stream into
  // a connection close.
  if ((!enable_half_close_ && result.end_stream_read_)) {
    result.end_stream_read_ = false;
    result.action_ = PostIoAction::Close;
  }

  read_end_stream_ |= result.end_stream_read_;
  if (result.bytes_processed_ != 0 || result.end_stream_read_ ||
      (latched_dispatch_buffered_data && read_buffer_->length() > 0)) {
    // Skip onRead if no bytes were processed unless we explicitly want to force onRead for
    // buffered data. For instance, skip onRead if the connection was closed without producing
    // more data.
    onRead(new_buffer_size);
  }

  // The read callback may have already closed the connection.
  if (result.action_ == PostIoAction::Close || bothSidesHalfClosed()) {
    ENVOY_CONN_LOG(debug, "remote close", *this);
    closeSocket(ConnectionEvent::RemoteClose);
  }
}

absl::optional<Connection::UnixDomainSocketPeerCredentials>
ConnectionImpl::unixSocketPeerCredentials() const {
  // TODO(snowp): Support non-linux platforms.
#ifndef SO_PEERCRED
  return absl::nullopt;
#else
  struct ucred ucred;
  socklen_t ucred_size = sizeof(ucred);
  int rc = socket_->getSocketOption(SOL_SOCKET, SO_PEERCRED, &ucred, &ucred_size).rc_;
  if (SOCKET_FAILURE(rc)) {
    return absl::nullopt;
  }

  return {{ucred.pid, ucred.uid, ucred.gid}};
#endif
}

void ConnectionImpl::onWriteReady() {
  ENVOY_CONN_LOG(trace, "write ready", *this);

  if (connecting_) {
    int error;
    socklen_t error_size = sizeof(error);
    RELEASE_ASSERT(socket_->getSocketOption(SOL_SOCKET, SO_ERROR, &error, &error_size).rc_ == 0,
                   "");

    if (error == 0) {
      ENVOY_CONN_LOG(debug, "connected", *this);
      connecting_ = false;
      transport_socket_->onConnected();
      // It's possible that we closed during the connect callback.
      if (state() != State::Open) {
        ENVOY_CONN_LOG(debug, "close during connected callback", *this);
        return;
      }
    } else {
      ENVOY_CONN_LOG(debug, "delayed connection error: {}", *this, error);
      closeSocket(ConnectionEvent::RemoteClose);
      return;
    }
  }

  IoResult result = transport_socket_->doWrite(*write_buffer_, write_end_stream_);
  ASSERT(!result.end_stream_read_); // The interface guarantees that only read operations set this.
  uint64_t new_buffer_size = write_buffer_->length();
  updateWriteBufferStats(result.bytes_processed_, new_buffer_size);

  // NOTE: If the delayed_close_timer_ is set, it must only trigger after a delayed_close_timeout_
  // period of inactivity from the last write event. Therefore, the timer must be reset to its
  // original timeout value unless the socket is going to be closed as a result of the doWrite().

  if (result.action_ == PostIoAction::Close) {
    // It is possible (though unlikely) for the connection to have already been closed during the
    // write callback. This can happen if we manage to complete the SSL handshake in the write
    // callback, raise a connected event, and close the connection.
    closeSocket(ConnectionEvent::RemoteClose);
  } else if ((inDelayedClose() && new_buffer_size == 0) || bothSidesHalfClosed()) {
    ENVOY_CONN_LOG(debug, "write flush complete", *this);
    if (delayed_close_state_ == DelayedCloseState::CloseAfterFlushAndWait) {
      ASSERT(delayed_close_timer_ != nullptr && delayed_close_timer_->enabled());
      if (result.bytes_processed_ > 0) {
        delayed_close_timer_->enableTimer(delayed_close_timeout_);
      }
    } else {
      ASSERT(bothSidesHalfClosed() || delayed_close_state_ == DelayedCloseState::CloseAfterFlush);
      closeConnectionImmediately();
    }
  } else {
    ASSERT(result.action_ == PostIoAction::KeepOpen);
    ASSERT(!delayed_close_timer_ || delayed_close_timer_->enabled());
    if (delayed_close_timer_ != nullptr && result.bytes_processed_ > 0) {
      delayed_close_timer_->enableTimer(delayed_close_timeout_);
    }
    if (result.bytes_processed_ > 0) {
      auto it = bytes_sent_callbacks_.begin();
      while (it != bytes_sent_callbacks_.end()) {
        if ((*it)(result.bytes_processed_)) {
          // move to the next callback.
          it++;
        } else {
          // remove the current callback.
          it = bytes_sent_callbacks_.erase(it);
        }

        // If a callback closes the socket, stop iterating.
        if (!ioHandle().isOpen()) {
          return;
        }
      }
    }
  }
}

void ConnectionImpl::updateReadBufferStats(uint64_t num_read, uint64_t new_size) {
  if (!connection_stats_) {
    return;
  }

  ConnectionImplUtility::updateBufferStats(num_read, new_size, last_read_buffer_size_,
                                           connection_stats_->read_total_,
                                           connection_stats_->read_current_);
}

void ConnectionImpl::updateWriteBufferStats(uint64_t num_written, uint64_t new_size) {
  if (!connection_stats_) {
    return;
  }

  ConnectionImplUtility::updateBufferStats(num_written, new_size, last_write_buffer_size_,
                                           connection_stats_->write_total_,
                                           connection_stats_->write_current_);
}

bool ConnectionImpl::bothSidesHalfClosed() {
  // If the write_buffer_ is not empty, then the end_stream has not been sent to the transport yet.
  return read_end_stream_ && write_end_stream_ && write_buffer_->length() == 0;
}

absl::string_view ConnectionImpl::transportFailureReason() const {
  return transport_socket_->failureReason();
}

absl::optional<std::chrono::milliseconds> ConnectionImpl::lastRoundTripTime() const {
  return socket_->lastRoundTripTime();
};

void ConnectionImpl::flushWriteBuffer() {
  if (state() == State::Open && write_buffer_->length() > 0) {
    onWriteReady();
  }
}

void ConnectionImpl::dumpState(std::ostream& os, int indent_level) const {
  const char* spaces = spacesForLevel(indent_level);
  os << spaces << "ConnectionImpl " << this << DUMP_MEMBER(connecting_) << DUMP_MEMBER(bind_error_)
     << DUMP_MEMBER(state()) << DUMP_MEMBER(read_buffer_limit_) << "\n";

  DUMP_DETAILS(socket_);
}

ServerConnectionImpl::ServerConnectionImpl(Event::Dispatcher& dispatcher,
                                           ConnectionSocketPtr&& socket,
                                           TransportSocketPtr&& transport_socket,
                                           StreamInfo::StreamInfo& stream_info, bool connected)
    : ConnectionImpl(dispatcher, std::move(socket), std::move(transport_socket), stream_info,
                     connected) {
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.always_nodelay")) {
    noDelay(true);
  }
}

void ServerConnectionImpl::setTransportSocketConnectTimeout(std::chrono::milliseconds timeout) {
  if (!transport_connect_pending_) {
    return;
  }
  if (transport_socket_connect_timer_ == nullptr) {
    transport_socket_connect_timer_ =
        dispatcher_.createScaledTimer(Event::ScaledTimerType::TransportSocketConnectTimeout,
                                      [this] { onTransportSocketConnectTimeout(); });
  }
  transport_socket_connect_timer_->enableTimer(timeout);
}

void ServerConnectionImpl::raiseEvent(ConnectionEvent event) {
  switch (event) {
  case ConnectionEvent::Connected:
  case ConnectionEvent::RemoteClose:
  case ConnectionEvent::LocalClose:
    transport_connect_pending_ = false;
    transport_socket_connect_timer_.reset();
  }
  ConnectionImpl::raiseEvent(event);
}

void ServerConnectionImpl::onTransportSocketConnectTimeout() {
  stream_info_.setConnectionTerminationDetails(kTransportSocketConnectTimeoutTerminationDetails);
  closeConnectionImmediately();
}

ClientConnectionImpl::ClientConnectionImpl(
    Event::Dispatcher& dispatcher, const Address::InstanceConstSharedPtr& remote_address,
    const Network::Address::InstanceConstSharedPtr& source_address,
    Network::TransportSocketPtr&& transport_socket,
    const Network::ConnectionSocket::OptionsSharedPtr& options)
    : ConnectionImpl(dispatcher, std::make_unique<ClientSocketImpl>(remote_address, options),
                     std::move(transport_socket), stream_info_, false),
      stream_info_(dispatcher.timeSource(), socket_->addressProviderSharedPtr()) {
  // There are no meaningful socket options or source address semantics for
  // non-IP sockets, so skip.
  if (remote_address->ip() != nullptr) {
    if (!Network::Socket::applyOptions(options, *socket_,
                                       envoy::config::core::v3::SocketOption::STATE_PREBIND)) {
      // Set a special error state to ensure asynchronous close to give the owner of the
      // ConnectionImpl a chance to add callbacks and detect the "disconnect".
      immediate_error_event_ = ConnectionEvent::LocalClose;
      // Trigger a write event to close this connection out-of-band.
      ioHandle().activateFileEvents(Event::FileReadyType::Write);
      return;
    }

    const Network::Address::InstanceConstSharedPtr* source = &source_address;

    if (socket_->addressProvider().localAddress()) {
      source = &socket_->addressProvider().localAddress();
    }

    if (*source != nullptr) {
      Api::SysCallIntResult result = socket_->bind(*source);
      if (result.rc_ < 0) {
        // TODO(lizan): consider add this error into transportFailureReason.
        ENVOY_LOG_MISC(debug, "Bind failure. Failed to bind to {}: {}", source->get()->asString(),
                       errorDetails(result.errno_));
        bind_error_ = true;
        // Set a special error state to ensure asynchronous close to give the owner of the
        // ConnectionImpl a chance to add callbacks and detect the "disconnect".
        immediate_error_event_ = ConnectionEvent::LocalClose;

        // Trigger a write event to close this connection out-of-band.
        ioHandle().activateFileEvents(Event::FileReadyType::Write);
      }
    }
  }
}

void ClientConnectionImpl::connect() {
  ENVOY_CONN_LOG(debug, "connecting to {}", *this,
                 socket_->addressProvider().remoteAddress()->asString());
  const Api::SysCallIntResult result = socket_->connect(socket_->addressProvider().remoteAddress());
  if (result.rc_ == 0) {
    if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.always_nodelay")) {
      noDelay(true);
    }
    // write will become ready.
    ASSERT(connecting_);
  } else {
    ASSERT(SOCKET_FAILURE(result.rc_));
#ifdef WIN32
    // winsock2 connect returns EWOULDBLOCK if the socket is non-blocking and the connection
    // cannot be completed immediately. We do not check for `EINPROGRESS` as that error is for
    // blocking operations.
    if (result.errno_ == SOCKET_ERROR_AGAIN) {
#else
    if (result.errno_ == SOCKET_ERROR_IN_PROGRESS) {
#endif
      ASSERT(connecting_);
      ENVOY_CONN_LOG(debug, "connection in progress", *this);
    } else {
      immediate_error_event_ = ConnectionEvent::RemoteClose;
      connecting_ = false;
      ENVOY_CONN_LOG(debug, "immediate connection error: {}", *this, result.errno_);

      // Trigger a write event. This is needed on macOS and seems harmless on Linux.
      ioHandle().activateFileEvents(Event::FileReadyType::Write);
    }
  }
}
} // namespace Network
} // namespace Envoy
