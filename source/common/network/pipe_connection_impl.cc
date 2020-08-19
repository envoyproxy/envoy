#include "common/network/pipe_connection_impl.h"

#include <atomic>
#include <cstdint>
#include <memory>

#include "envoy/common/exception.h"
#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/event/timer.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/network/transport_socket.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/network/address_impl.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/raw_buffer_socket.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Network {

ClientPipeImpl::ClientPipeImpl(Event::Dispatcher& dispatcher,
                               const Address::InstanceConstSharedPtr& remote_address,
                               const Address::InstanceConstSharedPtr& source_address,
                               TransportSocketPtr transport_socket,
                               const Network::ConnectionSocket::OptionsSharedPtr&)
    : ConnectionImplBase(dispatcher, next_global_id_++),
      transport_socket_(std::move(transport_socket)), stream_info_(dispatcher.timeSource()),
      filter_manager_(*this),
      read_buffer_([this]() -> void { this->onReadBufferLowWatermark(); },
                   [this]() -> void { this->onReadBufferHighWatermark(); },
                   []() -> void { /* TODO(adisuissa): Handle overflow watermark */ }),
      write_buffer_(dispatcher.getWatermarkFactory().create(
          [this]() -> void { this->onWriteBufferLowWatermark(); },
          [this]() -> void { this->onWriteBufferHighWatermark(); },
          []() -> void { /* TODO(adisuissa): Handle overflow watermark */ })),
      write_buffer_above_high_watermark_(false), detect_early_close_(true),
      enable_half_close_(false), read_end_stream_raised_(false), read_end_stream_(false),
      write_end_stream_(false), current_write_end_stream_(false), dispatch_buffered_data_(false),
      remote_address_(remote_address), source_address_(source_address),
      io_timer_(dispatcher.createTimer([this]() { onFileEvent(); })) {
  ENVOY_LOG_MISC(debug, "lambdai: client pipe C{} owns rb B{} and wb B{}", id(), read_buffer_.bid(),
                 write_buffer_->bid());

  connecting_ = true;
  transport_socket_->setTransportSocketCallbacks(*this);
}

ClientPipeImpl::~ClientPipeImpl() {
  ASSERT(!isOpen() && delayed_close_timer_ == nullptr,
         "ClientPipeImpl was unexpectedly torn down without being closed.");

  // In general we assume that owning code has called close() previously to the destructor being
  // run. This generally must be done so that callbacks run in the correct context (vs. deferred
  // deletion). Hence the assert above. However, call close() here just to be completely sure that
  // the fd is closed and make it more likely that we crash from a bad close callback.
  close(ConnectionCloseType::NoFlush);
}

void ClientPipeImpl::addWriteFilter(WriteFilterSharedPtr filter) {
  filter_manager_.addWriteFilter(filter);
}

void ClientPipeImpl::addFilter(FilterSharedPtr filter) { filter_manager_.addFilter(filter); }

void ClientPipeImpl::addReadFilter(ReadFilterSharedPtr filter) {
  filter_manager_.addReadFilter(filter);
}

bool ClientPipeImpl::initializeReadFilters() { return filter_manager_.initializeReadFilters(); }

void ClientPipeImpl::close(ConnectionCloseType type) {
  if (!isOpen()) {
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
        events_ |= (enable_half_close_ ? 0 : Event::FileReadyType::Closed);
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

    events_ |=
        (Event::FileReadyType::Write | (enable_half_close_ ? 0 : Event::FileReadyType::Closed));
  }
}

Connection::State ClientPipeImpl::state() const {
  if (!isOpen()) {
    return State::Closed;
  } else if (inDelayedClose()) {
    return State::Closing;
  } else {
    return State::Open;
  }
}

void ClientPipeImpl::closeConnectionImmediately() { closeSocket(ConnectionEvent::LocalClose); }

bool ClientPipeImpl::consumerWantsToRead() {
  return read_disable_count_ == 0 ||
         (read_disable_count_ == 1 && read_buffer_.highWatermarkTriggered());
}

void ClientPipeImpl::closeSocket(ConnectionEvent close_type) {
  if (!isOpen()) {
    ENVOY_LOG_MISC(debug, "lambdai: Closing socket C{} type {}", id(),
                   static_cast<int>(close_type));
    return;
  }
  ENVOY_LOG_MISC(debug, "lambdai: Closing socket C{} type {}", id(), static_cast<int>(close_type));

  // No need for a delayed close (if pending) now that the socket is being closed.
  if (delayed_close_timer_) {
    delayed_close_timer_->disableTimer();
    delayed_close_timer_ = nullptr;
  }

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

  is_open_ = false;

  // Call the base class directly as close() is called in the destructor.
  ClientPipeImpl::raiseEvent(close_type);

  // Close peer using the opposite event type.
  // TOOD(lambdai): guarantee peer exists.
  if (close_type == ConnectionEvent::LocalClose) {
    // TODO(lambdai): check is_open_ instead of peer
    if (peer_) {
      ENVOY_LOG_MISC(debug, "lambdai: notify peer C{} with close type {}", id(),
                     static_cast<int>(close_type));
      peer_->closeSocket(ConnectionEvent::RemoteClose);
      // This connection closes first and the peer don't have the chance to notify us.
      peer_ = nullptr;
    } else {
      ENVOY_LOG_MISC(debug, "lambdai: cannot notify peer C{} with close type {} since peer is gone",
                     id(), static_cast<int>(close_type));
    }
  } else {
    // Passively set peer to nullptr so futher peer operation won't crash.
    peer_ = nullptr;
  }
}

void ClientPipeImpl::noDelay(bool enable) {
  // Enable is noop while disable doesn't make sense in pipe.
  RELEASE_ASSERT(enable, "UserPipe is always no delay");
}

void ClientPipeImpl::onRead(uint64_t read_buffer_size) {
  if (inDelayedClose() || !consumerWantsToRead()) {
    return;
  }
  ASSERT(isOpen());

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
    // in ClientPipeImpl or strangeness in the OS events (epoll, kqueue, etc)
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

void ClientPipeImpl::enableHalfClose(bool enabled) {
  // This code doesn't correctly ensure that EV_CLOSE isn't set if reading is disabled
  // when enabling half-close. This could be fixed, but isn't needed right now, so just
  // ASSERT that it doesn't happen.
  ASSERT(!enabled || read_disable_count_ == 0);

  enable_half_close_ = enabled;
}

void ClientPipeImpl::readDisable(bool disable) {
  // Calls to readEnabled on a closed socket are considered to be an error.
  ASSERT(state() == State::Open);

  ENVOY_CONN_LOG(trace, "readDisable: disable={} disable_count={} state={} buffer_length={}", *this,
                 disable, read_disable_count_, static_cast<int>(state()), read_buffer_.length());

  // When we disable reads, we still allow for early close notifications (the equivalent of
  // EPOLLRDHUP for an epoll backend). For backends that support it, this allows us to apply
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

    if (detect_early_close_ && !enable_half_close_) {
      enableWriteClose();
    } else {
      enableWrite();
    }
  } else {
    ASSERT(read_disable_count_ != 0);
    --read_disable_count_;
    if (state() != State::Open) {
      // If readDisable is called on a closed connection, do not crash.
      return;
    }
    // Now read from peer buffer.
    if (read_disable_count_ == 0) {
      // We never ask for both early close and read at the same time. If we are reading, we want to
      // consume all available data.
      enableWriteRead();
      // Pipe implementation is not signaled if buffersource socket has data. Meanwhile we must not
      // trigger the inline read since the readDisable() might be in the context of buffer
      // consuming. We need to manually trigger it.
      ENVOY_LOG_MISC(debug, "lambdai: C{} will do transport socket read", id());
      setReadBufferReady();
    }
    if (consumerWantsToRead() && read_buffer_.length() > 0) {
      // Drain the read buffer first. Won't read from peered buffer since dispatch_buffered_data_ is
      // on.
      // If the connection has data buffered there's no guarantee there's also data in the kernel
      // which will kick off the filter chain. Alternately if the read buffer has data the fd could
      // be read disabled. To handle these cases, fake an event to make sure the buffered data gets
      // processed regardless and ensure that we dispatch it via onRead.
      ENVOY_LOG_MISC(debug, "lambdai: C{} will dispatch read buffer", id());
      dispatch_buffered_data_ = true;
      setReadBufferReady();
    }
  }
}

void ClientPipeImpl::raiseEvent(ConnectionEvent event) {
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

bool ClientPipeImpl::readEnabled() const {
  // Calls to readEnabled on a closed socket are considered to be an error.
  ASSERT(state() == State::Open);
  return read_disable_count_ == 0;
}

void ClientPipeImpl::addBytesSentCallback(BytesSentCb cb) {
  bytes_sent_callbacks_.emplace_back(cb);
}

void ClientPipeImpl::rawWrite(Buffer::Instance& data, bool end_stream) {
  write(data, end_stream, false);
}

void ClientPipeImpl::write(Buffer::Instance& data, bool end_stream) {
  write(data, end_stream, true);
}

void ClientPipeImpl::write(Buffer::Instance& data, bool end_stream, bool through_filter_chain) {
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
    onWriteReady();
    RELEASE_ASSERT(!connecting_, "Userspace pipe should not see connecting state.");
  }
}

void ClientPipeImpl::setBufferLimits(uint32_t limit) {
  read_buffer_limit_ = limit;

  // Due to the fact that writes to the connection and flushing data from the connection are done
  // asynchronously, we have the option of either setting the watermarks aggressively, and regularly
  // enabling/disabling reads from the socket, or allowing more data, but then not triggering
  // based on watermarks until 2x the data is buffered in the common case. Given these are all soft
  // limits we err on the side of buffering more triggering watermark callbacks less often.
  //
  // Given the current implementation for straight up TCP proxying, the common case is reading
  // |limit| bytes through the socket, passing |limit| bytes to the connection (triggering the high
  // watermarks) and the immediately draining |limit| bytes to the socket (triggering the low
  // watermarks). We avoid this by setting the high watermark to limit + 1 so a single read will
  // not trigger watermarks if the socket is not blocked.
  //
  // If the connection class is changed to write to the buffer and flush to the socket in the same
  // stack then instead of checking watermarks after the write and again after the flush it can
  // check once after both operations complete. At that point it would be better to change the high
  // watermark from |limit + 1| to |limit| as the common case (move |limit| bytes, flush |limit|
  // bytes) would not trigger watermarks but a blocked socket (move |limit| bytes, flush 0 bytes)
  // would result in respecting the exact buffer limit.
  if (limit > 0) {
    static_cast<Buffer::WatermarkBuffer*>(write_buffer_.get())->setWatermarks(limit + 1);
    read_buffer_.setWatermarks(limit + 1);
  }
}

void ClientPipeImpl::onReadBufferLowWatermark() {
  ENVOY_CONN_LOG(debug, "onBelowReadBufferLowWatermark", *this);
  if (state() == State::Open) {
    readDisable(false);
  }
}

void ClientPipeImpl::onReadBufferHighWatermark() {
  ENVOY_CONN_LOG(debug, "onAboveReadBufferHighWatermark", *this);
  if (state() == State::Open) {
    readDisable(true);
  }
}

void ClientPipeImpl::onWriteBufferLowWatermark() {
  ENVOY_CONN_LOG(debug, "onBelowWriteBufferLowWatermark", *this);
  ASSERT(write_buffer_above_high_watermark_);
  write_buffer_above_high_watermark_ = false;
  for (ConnectionCallbacks* callback : callbacks_) {
    callback->onBelowWriteBufferLowWatermark();
  }
}

void ClientPipeImpl::onWriteBufferHighWatermark() {
  ENVOY_CONN_LOG(debug, "onAboveWriteBufferHighWatermark", *this);
  ASSERT(!write_buffer_above_high_watermark_);
  write_buffer_above_high_watermark_ = true;
  for (ConnectionCallbacks* callback : callbacks_) {
    callback->onAboveWriteBufferHighWatermark();
  }
}

void ClientPipeImpl::onFileEvent() {
  auto events = events_;
  events_ = 0;
  onFileEvent(events);
}
void ClientPipeImpl::onFileEvent(uint32_t events) {
  ENVOY_CONN_LOG(trace, "socket event: {}", *this, events);

  if (immediate_error_event_ != ConnectionEvent::Connected) {
    ENVOY_CONN_LOG(debug, "raising immediate error", *this);
    closeSocket(immediate_error_event_);
    return;
  }

  if (events & Event::FileReadyType::Closed) {
    // We never ask for both early close and read at the same time. If we are reading, we want to
    // consume all available data.
    // ASSERT(!(events & Event::FileReadyType::Read));
    ENVOY_CONN_LOG(debug, "remote early close", *this);
    closeSocket(ConnectionEvent::RemoteClose);
    return;
  }

  if (events & Event::FileReadyType::Write) {
    onWriteReady();
  }

  // It's possible for a write event callback to close the socket (which will cause fd_ to be -1).
  // In this case ignore write event processing.
  if (isOpen() && (events & Event::FileReadyType::Read)) {
    onReadReady();
  }
}

void ClientPipeImpl::onReadReady() {
  ENVOY_CONN_LOG(trace, "read ready. dispatch_buffered_data={}", *this, dispatch_buffered_data_);
  const bool latched_dispatch_buffered_data = dispatch_buffered_data_;
  dispatch_buffered_data_ = false;

  // ASSERT(!connecting_);

  // We get here while read disabled in two ways.
  // 1) There was a call to setReadBufferReady(), for example if a raw buffer socket ceded due to
  //    shouldDrainReadBuffer(). In this case we defer the event until the socket is read enabled.
  // 2) The consumer of connection data called readDisable(true), and instead of reading from the
  //    socket we simply need to dispatch already read data.
  if (read_disable_count_ != 0) {
    if (latched_dispatch_buffered_data && consumerWantsToRead()) {
      onRead(read_buffer_.length());
    }
    return;
  }

  IoResult result = transport_socket_->doRead(read_buffer_);
  uint64_t new_buffer_size = read_buffer_.length();
  updateReadBufferStats(result.bytes_processed_, new_buffer_size);

  ENVOY_LOG_MISC(debug,
                 "lambdai: C{} transport read, buffer_size={} enable_half_close_={}, "
                 "end_stream_read_={}, result.action = {}",
                 id(), new_buffer_size, enable_half_close_, result.end_stream_read_,
                 result.action_ == PostIoAction::KeepOpen ? "open" : "close");

  // If this connection doesn't have half-close semantics, translate end_stream into
  // a connection close.
  if ((!enable_half_close_ && result.end_stream_read_)) {
    result.end_stream_read_ = false;
    result.action_ = PostIoAction::Close;
  }

  read_end_stream_ |= result.end_stream_read_;
  if (result.bytes_processed_ != 0 || result.end_stream_read_ ||
      (latched_dispatch_buffered_data && read_buffer_.length() > 0)) {
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
ClientPipeImpl::unixSocketPeerCredentials() const {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

void ClientPipeImpl::onWriteReady() {
  ENVOY_CONN_LOG(trace, "write ready", *this);

  if (connecting_) {
    connecting_ = false;
    transport_socket_->onConnected();
    if (state() != State::Open) {
      ENVOY_CONN_LOG(debug, "close during connected callback", *this);
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
    peer_->onReadReady();

    ENVOY_CONN_LOG(debug, "write flush complete", *this);
    if (delayed_close_state_ == DelayedCloseState::CloseAfterFlushAndWait) {
      ASSERT(delayed_close_timer_ != nullptr);
      delayed_close_timer_->enableTimer(delayed_close_timeout_);
    } else {
      ASSERT(bothSidesHalfClosed() || delayed_close_state_ == DelayedCloseState::CloseAfterFlush);
      closeConnectionImmediately();
    }
  } else {
    ASSERT(result.action_ == PostIoAction::KeepOpen);
    ENVOY_LOG_MISC(debug,
                   "lambdai: {} trigger peer read ready, with bytes={}, write_end_stream_={}",
                   connection().id(), result.bytes_processed_, write_end_stream_);
    peer_->onReadReady();
    if (delayed_close_timer_ != nullptr) {
      delayed_close_timer_->enableTimer(delayed_close_timeout_);
    }
    if (result.bytes_processed_ > 0) {
      for (BytesSentCb& cb : bytes_sent_callbacks_) {
        cb(result.bytes_processed_);

        // If a callback closes the socket, stop iterating.
        if (!isOpen()) {
          return;
        }
      }
    }
  }
}

void ClientPipeImpl::updateReadBufferStats(uint64_t num_read, uint64_t new_size) {
  if (!connection_stats_) {
    return;
  }

  ConnectionImplUtility::updateBufferStats(num_read, new_size, last_read_buffer_size_,
                                           connection_stats_->read_total_,
                                           connection_stats_->read_current_);
}

void ClientPipeImpl::updateWriteBufferStats(uint64_t num_written, uint64_t new_size) {
  if (!connection_stats_) {
    return;
  }

  ConnectionImplUtility::updateBufferStats(num_written, new_size, last_write_buffer_size_,
                                           connection_stats_->write_total_,
                                           connection_stats_->write_current_);
}

bool ClientPipeImpl::bothSidesHalfClosed() {
  // If the write_buffer_ is not empty, then the end_stream has not been sent to the transport yet.
  return read_end_stream_ && write_end_stream_ && write_buffer_->length() == 0;
}

absl::string_view ClientPipeImpl::transportFailureReason() const {
  return transport_socket_->failureReason();
}

void ClientPipeImpl::flushWriteBuffer() {
  if (state() == State::Open && write_buffer_->length() > 0) {
    onWriteReady();
  }
}

void ClientPipeImpl::connect() {
  ENVOY_CONN_LOG(debug, "connecting to {}", *this, remote_address_->asString());
  // Try read and write.
  // TODO(lambdai): Verify that the timer could be cancelled if buffer is empty.
  events_ |= (Event::FileReadyType::Write | Event::FileReadyType::Read);
  io_timer_->enableTimer(std::chrono::milliseconds(0));
}

ServerPipeImpl::ServerPipeImpl(Event::Dispatcher& dispatcher,
                               const Address::InstanceConstSharedPtr& remote_address,
                               const Address::InstanceConstSharedPtr& source_address,
                               TransportSocketPtr transport_socket,
                               const Network::ConnectionSocket::OptionsSharedPtr&)
    : ConnectionImplBase(dispatcher, next_global_id_++),
      transport_socket_(std::move(transport_socket)), filter_manager_(*this),
      read_buffer_([this]() -> void { this->onReadBufferLowWatermark(); },
                   [this]() -> void { this->onReadBufferHighWatermark(); },
                   []() -> void { /* TODO(adisuissa): Handle overflow watermark */ }),
      write_buffer_(dispatcher.getWatermarkFactory().create(
          [this]() -> void { this->onWriteBufferLowWatermark(); },
          [this]() -> void { this->onWriteBufferHighWatermark(); },
          []() -> void { /* TODO(adisuissa): Handle overflow watermark */ })),
      write_buffer_above_high_watermark_(false), detect_early_close_(true),
      enable_half_close_(false), read_end_stream_raised_(false), read_end_stream_(false),
      write_end_stream_(false), current_write_end_stream_(false), dispatch_buffered_data_(false),
      remote_address_(remote_address), source_address_(source_address),
      io_timer_(dispatcher.createTimer([this]() { onFileEvent(); })) {
  ENVOY_LOG_MISC(debug, "lambdai: server pipe C{} owns rb B{} and wb B{}", id(), read_buffer_.bid(),
                 write_buffer_->bid());

  transport_socket_->setTransportSocketCallbacks(*this);
}

ServerPipeImpl::~ServerPipeImpl() {
  ASSERT(!isOpen() && delayed_close_timer_ == nullptr,
         "ServerPipeImpl was unexpectedly torn down without being closed.");

  // In general we assume that owning code has called close() previously to the destructor being
  // run. This generally must be done so that callbacks run in the correct context (vs. deferred
  // deletion). Hence the assert above. However, call close() here just to be completely sure that
  // the fd is closed and make it more likely that we crash from a bad close callback.
  close(ConnectionCloseType::NoFlush);
}

void ServerPipeImpl::addWriteFilter(WriteFilterSharedPtr filter) {
  filter_manager_.addWriteFilter(filter);
}

void ServerPipeImpl::addFilter(FilterSharedPtr filter) { filter_manager_.addFilter(filter); }

void ServerPipeImpl::addReadFilter(ReadFilterSharedPtr filter) {
  filter_manager_.addReadFilter(filter);
}

bool ServerPipeImpl::initializeReadFilters() { return filter_manager_.initializeReadFilters(); }

void ServerPipeImpl::close(ConnectionCloseType) {
  if (!isOpen()) {
    ENVOY_LOG_MISC(debug, "lambdai: attempt to close a closed server pipe CS{}", id());
    return;
  }
  closeConnectionImmediately();
}

Connection::State ServerPipeImpl::state() const {
  if (!isOpen()) {
    return State::Closed;
  } else if (inDelayedClose()) {
    return State::Closing;
  } else {
    return State::Open;
  }
}

void ServerPipeImpl::closeConnectionImmediately() { closeSocket(ConnectionEvent::LocalClose); }

bool ServerPipeImpl::consumerWantsToRead() {
  return read_disable_count_ == 0 ||
         (read_disable_count_ == 1 && read_buffer_.highWatermarkTriggered());
}

void ServerPipeImpl::closeSocket(ConnectionEvent close_type) {
  if (!isOpen()) {
    ENVOY_LOG_MISC(debug, "lambdai: Closing socket C{} type {}", id(),
                   static_cast<int>(close_type));
    return;
  }
  ENVOY_LOG_MISC(debug, "lambdai: Closing socket C{} type {}", id(), static_cast<int>(close_type));

  // No need for a delayed close (if pending) now that the socket is being closed.
  if (delayed_close_timer_) {
    delayed_close_timer_->disableTimer();
    delayed_close_timer_ = nullptr;
  }

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

  is_open_ = false;

  // Call the base class directly as close() is called in the destructor.
  ServerPipeImpl::raiseEvent(close_type);

  // Close peer using the opposite event type.
  // TOOD(lambdai): guarantee peer exists.
  if (close_type == ConnectionEvent::LocalClose) {
    // TODO(lambdai): check is_open_ instead of peer
    if (peer_) {
      ENVOY_LOG_MISC(debug, "lambdai: notify peer C{} with close type {}", id(),
                     static_cast<int>(close_type));
      peer_->closeSocket(ConnectionEvent::RemoteClose);
      // This connection closes first and the peer don't have the chance to notify us.
      peer_ = nullptr;
    } else {
      ENVOY_LOG_MISC(debug, "lambdai: cannot notify peer C{} with close type {} since peer is gone",
                     id(), static_cast<int>(close_type));
    }
  } else {
    // Passively set peer to nullptr so futher peer operation won't crash.
    peer_ = nullptr;
  }
}

void ServerPipeImpl::noDelay(bool enable) {
  // Enable is noop while disable doesn't make sense in pipe.
  RELEASE_ASSERT(enable, "UserPipe is always no delay");
}

void ServerPipeImpl::onRead(uint64_t read_buffer_size) {
  if (inDelayedClose() || !consumerWantsToRead()) {
    return;
  }
  ASSERT(isOpen());

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

void ServerPipeImpl::enableHalfClose(bool enabled) {
  // This code doesn't correctly ensure that EV_CLOSE isn't set if reading is disabled
  // when enabling half-close. This could be fixed, but isn't needed right now, so just
  // ASSERT that it doesn't happen.
  ASSERT(!enabled || read_disable_count_ == 0);

  enable_half_close_ = enabled;
}

void ServerPipeImpl::readDisable(bool disable) {
  // Calls to readEnabled on a closed socket are considered to be an error.
  ASSERT(state() == State::Open);

  ENVOY_CONN_LOG(trace, "readDisable: disable={} disable_count={} state={} buffer_length={}", *this,
                 disable, read_disable_count_, static_cast<int>(state()), read_buffer_.length());

  // When we disable reads, we still allow for early close notifications (the equivalent of
  // EPOLLRDHUP for an epoll backend). For backends that support it, this allows us to apply
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
      enableWriteClose();
    } else {
      enableWrite();
    }
  } else {
    ASSERT(read_disable_count_ != 0);
    --read_disable_count_;
    if (state() != State::Open) {
      // If readDisable is called on a closed connection, do not crash.
      return;
    }

    // Now read from peer buffer.
    if (read_disable_count_ == 0) {
      // We never ask for both early close and read at the same time. If we are reading, we want to
      // consume all available data.
      enableWriteRead();
      // Pipe implementation is not signaled if buffersource socket has data. Meanwhile we must not
      // trigger the inline read since the readDisable() might be in the context of buffer
      // consuming. We need to manually trigger it.
      ENVOY_LOG_MISC(debug, "lambdai: C{} will do transport socket read", id());
      setReadBufferReady();
    }
    if (consumerWantsToRead() && read_buffer_.length() > 0) {
      // Drain the read buffer first. Won't read from peered buffer since dispatch_buffered_data_ is
      // on. If the connection has data buffered there's no guarantee there's also data in the
      // kernel which will kick off the filter chain. Alternately if the read buffer has data the fd
      // could be read disabled. To handle these cases, fake an event to make sure the buffered data
      // gets processed regardless and ensure that we dispatch it via onRead.
      ENVOY_LOG_MISC(debug, "lambdai: C{} will dispatch read buffer", id());
      dispatch_buffered_data_ = true;
      setReadBufferReady();
    }
  }
}

void ServerPipeImpl::raiseEvent(ConnectionEvent event) {
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

bool ServerPipeImpl::readEnabled() const {
  // Calls to readEnabled on a closed socket are considered to be an error.
  ASSERT(state() == State::Open);
  return read_disable_count_ == 0;
}

void ServerPipeImpl::addBytesSentCallback(BytesSentCb cb) {
  bytes_sent_callbacks_.emplace_back(cb);
}

void ServerPipeImpl::rawWrite(Buffer::Instance& data, bool end_stream) {
  write(data, end_stream, false);
}

void ServerPipeImpl::write(Buffer::Instance& data, bool end_stream) {
  write(data, end_stream, true);
}

void ServerPipeImpl::write(Buffer::Instance& data, bool end_stream, bool through_filter_chain) {
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
    onWriteReady();
  }
}

void ServerPipeImpl::setBufferLimits(uint32_t limit) {
  read_buffer_limit_ = limit;

  // Due to the fact that writes to the connection and flushing data from the connection are done
  // asynchronously, we have the option of either setting the watermarks aggressively, and regularly
  // enabling/disabling reads from the socket, or allowing more data, but then not triggering
  // based on watermarks until 2x the data is buffered in the common case. Given these are all soft
  // limits we err on the side of buffering more triggering watermark callbacks less often.
  //
  // Given the current implementation for straight up TCP proxying, the common case is reading
  // |limit| bytes through the socket, passing |limit| bytes to the connection (triggering the high
  // watermarks) and the immediately draining |limit| bytes to the socket (triggering the low
  // watermarks). We avoid this by setting the high watermark to limit + 1 so a single read will
  // not trigger watermarks if the socket is not blocked.
  //
  // If the connection class is changed to write to the buffer and flush to the socket in the same
  // stack then instead of checking watermarks after the write and again after the flush it can
  // check once after both operations complete. At that point it would be better to change the high
  // watermark from |limit + 1| to |limit| as the common case (move |limit| bytes, flush |limit|
  // bytes) would not trigger watermarks but a blocked socket (move |limit| bytes, flush 0 bytes)
  // would result in respecting the exact buffer limit.
  if (limit > 0) {
    static_cast<Buffer::WatermarkBuffer*>(write_buffer_.get())->setWatermarks(limit + 1);
    read_buffer_.setWatermarks(limit + 1);
  }
}

void ServerPipeImpl::onReadBufferLowWatermark() {
  ENVOY_CONN_LOG(debug, "onBelowReadBufferLowWatermark", *this);
  if (state() == State::Open) {
    readDisable(false);
  }
}

void ServerPipeImpl::onReadBufferHighWatermark() {
  ENVOY_CONN_LOG(debug, "onAboveReadBufferHighWatermark", *this);
  if (state() == State::Open) {
    readDisable(true);
  }
}

void ServerPipeImpl::onWriteBufferLowWatermark() {
  ENVOY_CONN_LOG(debug, "onBelowWriteBufferLowWatermark", *this);
  ASSERT(write_buffer_above_high_watermark_);
  write_buffer_above_high_watermark_ = false;
  for (ConnectionCallbacks* callback : callbacks_) {
    callback->onBelowWriteBufferLowWatermark();
  }
}

void ServerPipeImpl::onWriteBufferHighWatermark() {
  ENVOY_CONN_LOG(debug, "onAboveWriteBufferHighWatermark", *this);
  ASSERT(!write_buffer_above_high_watermark_);
  write_buffer_above_high_watermark_ = true;
  for (ConnectionCallbacks* callback : callbacks_) {
    callback->onAboveWriteBufferHighWatermark();
  }
}

void ServerPipeImpl::onFileEvent() {
  auto events = events_;
  events_ = 0;
  onFileEvent(events);
}
void ServerPipeImpl::onFileEvent(uint32_t events) {
  ENVOY_CONN_LOG(trace, "socket event: {}", *this, events);

  if (immediate_error_event_ != ConnectionEvent::Connected) {
    ENVOY_CONN_LOG(debug, "raising immediate error", *this);
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
  if (isOpen() && (events & Event::FileReadyType::Read)) {
    onReadReady();
  }
}

void ServerPipeImpl::onReadReady() {
  ENVOY_CONN_LOG(trace, "read ready. dispatch_buffered_data={}", *this, dispatch_buffered_data_);
  const bool latched_dispatch_buffered_data = dispatch_buffered_data_;
  dispatch_buffered_data_ = false;

  // We get here while read disabled in two ways.
  // 1) There was a call to setReadBufferReady(), for example if a raw buffer socket ceded due to
  //    shouldDrainReadBuffer(). In this case we defer the event until the socket is read enabled.
  // 2) The consumer of connection data called readDisable(true), and instead of reading from the
  //    socket we simply need to dispatch already read data.
  if (read_disable_count_ != 0) {
    if (latched_dispatch_buffered_data && consumerWantsToRead()) {
      onRead(read_buffer_.length());
    }
    return;
  }

  IoResult result = transport_socket_->doRead(read_buffer_);
  uint64_t new_buffer_size = read_buffer_.length();
  updateReadBufferStats(result.bytes_processed_, new_buffer_size);

  // If this connection doesn't have half-close semantics, translate end_stream into
  // a connection close.
  if ((!enable_half_close_ && result.end_stream_read_)) {
    result.end_stream_read_ = false;
    result.action_ = PostIoAction::Close;
  }

  read_end_stream_ |= result.end_stream_read_;
  if (result.bytes_processed_ != 0 || result.end_stream_read_ ||
      (latched_dispatch_buffered_data && read_buffer_.length() > 0)) {
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
ServerPipeImpl::unixSocketPeerCredentials() const {
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

void ServerPipeImpl::onWriteReady() {
  ENVOY_CONN_LOG(trace, "write ready", *this);

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
    peer_->onReadReady();
    ENVOY_CONN_LOG(debug, "write flush complete", *this);
    if (delayed_close_state_ == DelayedCloseState::CloseAfterFlushAndWait) {
      ASSERT(delayed_close_timer_ != nullptr);
      delayed_close_timer_->enableTimer(delayed_close_timeout_);
    } else {
      ASSERT(bothSidesHalfClosed() || delayed_close_state_ == DelayedCloseState::CloseAfterFlush);
      closeConnectionImmediately();
    }
  } else {
    ASSERT(result.action_ == PostIoAction::KeepOpen);
    ENVOY_LOG_MISC(debug, "lambdai: C{} trigger peer read ready", connection().id());
    peer_->onReadReady();
    if (delayed_close_timer_ != nullptr) {
      delayed_close_timer_->enableTimer(delayed_close_timeout_);
    }
    if (result.bytes_processed_ > 0) {
      for (BytesSentCb& cb : bytes_sent_callbacks_) {
        cb(result.bytes_processed_);

        // If a callback closes the socket, stop iterating.
        if (!isOpen()) {
          return;
        }
      }
    }
  }
}

void ServerPipeImpl::updateReadBufferStats(uint64_t num_read, uint64_t new_size) {
  if (!connection_stats_) {
    return;
  }

  ConnectionImplUtility::updateBufferStats(num_read, new_size, last_read_buffer_size_,
                                           connection_stats_->read_total_,
                                           connection_stats_->read_current_);
}

void ServerPipeImpl::updateWriteBufferStats(uint64_t num_written, uint64_t new_size) {
  if (!connection_stats_) {
    return;
  }

  ConnectionImplUtility::updateBufferStats(num_written, new_size, last_write_buffer_size_,
                                           connection_stats_->write_total_,
                                           connection_stats_->write_current_);
}

bool ServerPipeImpl::bothSidesHalfClosed() {
  // If the write_buffer_ is not empty, then the end_stream has not been sent to the transport yet.
  return read_end_stream_ && write_end_stream_ && write_buffer_->length() == 0;
}

absl::string_view ServerPipeImpl::transportFailureReason() const {
  return EMPTY_STRING; // from raw buffer ts
}

void ServerPipeImpl::flushWriteBuffer() {
  if (state() == State::Open && write_buffer_->length() > 0) {
    onWriteReady();
  }
}

} // namespace Network
} // namespace Envoy
