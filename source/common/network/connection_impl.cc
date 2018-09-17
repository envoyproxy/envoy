#include "common/network/connection_impl.h"

#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>

#include "envoy/common/exception.h"
#include "envoy/event/timer.h"
#include "envoy/network/filter.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/network/address_impl.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/raw_buffer_socket.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Network {

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
                               TransportSocketPtr&& transport_socket, bool connected)
    : transport_socket_(std::move(transport_socket)), filter_manager_(*this, *this),
      socket_(std::move(socket)), write_buffer_(dispatcher.getWatermarkFactory().create(
                                      [this]() -> void { this->onLowWatermark(); },
                                      [this]() -> void { this->onHighWatermark(); })),
      dispatcher_(dispatcher), id_(next_global_id_++) {
  // Treat the lack of a valid fd (which in practice only happens if we run out of FDs) as an OOM
  // condition and just crash.
  RELEASE_ASSERT(fd() != -1, "");

  if (!connected) {
    connecting_ = true;
  }

  // We never ask for both early close and read at the same time. If we are reading, we want to
  // consume all available data.
  file_event_ = dispatcher_.createFileEvent(
      fd(), [this](uint32_t events) -> void { onFileEvent(events); }, Event::FileTriggerType::Edge,
      Event::FileReadyType::Read | Event::FileReadyType::Write);

  transport_socket_->setTransportSocketCallbacks(*this);
}

ConnectionImpl::~ConnectionImpl() {
  ASSERT(fd() == -1, "ConnectionImpl was unexpectedly torn down without being closed.");

  if (delayed_close_timer_) {
    // It's ok to disable even if the timer has already fired.
    delayed_close_timer_->disableTimer();
  }

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

bool ConnectionImpl::initializeReadFilters() { return filter_manager_.initializeReadFilters(); }

void ConnectionImpl::close(ConnectionCloseType type) {
  if (fd() == -1) {
    return;
  }

  uint64_t data_to_write = write_buffer_->length();
  ENVOY_CONN_LOG(debug, "closing data_to_write={} type={}", *this, data_to_write, enumToInt(type));
  if (data_to_write == 0 || type == ConnectionCloseType::NoFlush ||
      !transport_socket_->canFlushClose()) {
    if (data_to_write > 0) {
      // We aren't going to wait to flush, but try to write as much as we can if there is pending
      // data.
      transport_socket_->doWrite(*write_buffer_, true);
    }

    closeSocket(ConnectionEvent::LocalClose);
  } else {
    ASSERT(type == ConnectionCloseType::FlushWrite ||
           type == ConnectionCloseType::FlushWriteAndDelay);

    // No need to continue if a FlushWrite/FlushWriteAndDelay has already been issued and there is a
    // pending delayed close.
    //
    // An example of this condition manifests when a downstream connection is closed early by Envoy,
    // such as when a route can't be matched:
    //   In ConnectionManagerImpl::onData()
    //     1) Via codec_->dispatch(), a local reply with a 404 is sent to the client
    //       a) ConnectionManagerImpl::doEndStream() issues the first connection close() via
    //          ConnectionManagerImpl::checkForDeferredClose()
    //     2) A second close is issued by a subsequent call to
    //        ConnectionManagerImpl::checkForDeferredClose() prior to returning from onData()
    if (delayed_close_) {
      return;
    }

    delayed_close_ = true;
    const bool delayed_close_timeout_set = delayedCloseTimeout().count() > 0;

    // NOTE: the delayed close timeout (if set) affects both FlushWrite and FlushWriteAndDelay
    // closes:
    //   1. For FlushWrite, the timeout sets an upper bound on how long to wait for the flush to
    //   complete before the connection is locally closed.
    //   2. For FlushWriteAndDelay, the timeout specifies an upper bound on how long to wait for the
    //   flush to complete and the peer to close the connection before it is locally closed.

    // All close types that follow do not actually close() the socket immediately so that buffered
    // data can be written. However, we do want to stop reading to apply TCP backpressure.
    read_enabled_ = false;

    // Force a closeSocket() after the write buffer is flushed if the close_type calls for it or if
    // no delayed close timeout is set.
    close_after_flush_ = !delayed_close_timeout_set || type == ConnectionCloseType::FlushWrite;

    // Create and activate a timer which will immediately close the connection if triggered.
    // A config value of 0 disables the timeout.
    if (delayed_close_timeout_set) {
      delayed_close_timer_ = dispatcher_.createTimer([this]() -> void { onDelayedCloseTimeout(); });
      ENVOY_CONN_LOG(debug, "setting delayed close timer with timeout {} ms", *this,
                     delayedCloseTimeout().count());
      delayed_close_timer_->enableTimer(delayedCloseTimeout());
    }

    file_event_->setEnabled(Event::FileReadyType::Write |
                            (enable_half_close_ ? 0 : Event::FileReadyType::Closed));
  }
}

Connection::State ConnectionImpl::state() const {
  if (fd() == -1) {
    return State::Closed;
  } else if (delayed_close_) {
    return State::Closing;
  } else {
    return State::Open;
  }
}

void ConnectionImpl::closeSocket(ConnectionEvent close_type) {
  if (fd() == -1) {
    return;
  }

  ENVOY_CONN_LOG(debug, "closing socket: {}", *this, static_cast<uint32_t>(close_type));
  transport_socket_->closeSocket(close_type);

  // Drain input and output buffers.
  updateReadBufferStats(0, 0);
  updateWriteBufferStats(0, 0);
  connection_stats_.reset();

  file_event_.reset();
  socket_->close();

  raiseEvent(close_type);
}

Event::Dispatcher& ConnectionImpl::dispatcher() { return dispatcher_; }

void ConnectionImpl::noDelay(bool enable) {
  // There are cases where a connection to localhost can immediately fail (e.g., if the other end
  // does not have enough fds, reaches a backlog limit, etc.). Because we run with deferred error
  // events, the calling code may not yet know that the connection has failed. This is one call
  // where we go outside of libevent and hit the fd directly and this case can fail if the fd is
  // invalid. For this call instead of plumbing through logic that will immediately indicate that a
  // connect failed, we will just ignore the noDelay() call if the socket is invalid since error is
  // going to be raised shortly anyway and it makes the calling code simpler.
  if (fd() == -1) {
    return;
  }

  // Don't set NODELAY for unix domain sockets
  sockaddr addr;
  socklen_t len = sizeof(addr);
  int rc = getsockname(fd(), &addr, &len);
  RELEASE_ASSERT(rc == 0, "");

  if (addr.sa_family == AF_UNIX) {
    return;
  }

  // Set NODELAY
  int new_value = enable;
  rc = setsockopt(fd(), IPPROTO_TCP, TCP_NODELAY, &new_value, sizeof(new_value));
#ifdef __APPLE__
  if (-1 == rc && errno == EINVAL) {
    // Sometimes occurs when the connection is not yet fully formed. Empirically, TCP_NODELAY is
    // enabled despite this result.
    return;
  }
#endif

  RELEASE_ASSERT(0 == rc, "");
}

uint64_t ConnectionImpl::id() const { return id_; }

void ConnectionImpl::onRead(uint64_t read_buffer_size) {
  if (!read_enabled_) {
    return;
  }

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
  ASSERT(!enabled || read_enabled_);

  enable_half_close_ = enabled;
}

void ConnectionImpl::readDisable(bool disable) {
  ASSERT(state() == State::Open);
  if (state() != State::Open || file_event_ == nullptr) {
    // If readDisable is called on a closed connection in error, do not crash.
    return;
  }

  ENVOY_CONN_LOG(trace, "readDisable: enabled={} disable={}", *this, read_enabled_, disable);

  // When we disable reads, we still allow for early close notifications (the equivalent of
  // EPOLLRDHUP for an epoll backend). For backends that support it, this allows us to apply
  // back pressure at the kernel layer, but still get timely notification of a FIN. Note that
  // we are not guaranteed to get notified, so even if the remote has closed, we may not know
  // until we try to write. Further note that currently we optionally don't correctly handle half
  // closed TCP connections in the sense that we assume that a remote FIN means the remote intends a
  // full close.
  if (disable) {
    if (!read_enabled_) {
      ++read_disable_count_;
      return;
    }
    ASSERT(read_enabled_);
    read_enabled_ = false;

    // If half-close semantics are enabled, we never want early close notifications; we
    // always want to read all available data, even if the other side has closed.
    if (detect_early_close_ && !enable_half_close_) {
      file_event_->setEnabled(Event::FileReadyType::Write | Event::FileReadyType::Closed);
    } else {
      file_event_->setEnabled(Event::FileReadyType::Write);
    }
  } else {
    if (read_disable_count_ > 0) {
      --read_disable_count_;
      return;
    }
    ASSERT(!read_enabled_);
    read_enabled_ = true;
    // We never ask for both early close and read at the same time. If we are reading, we want to
    // consume all available data.
    file_event_->setEnabled(Event::FileReadyType::Read | Event::FileReadyType::Write);
    // If the connection has data buffered there's no guarantee there's also data in the kernel
    // which will kick off the filter chain. Instead fake an event to make sure the buffered data
    // gets processed regardless.
    if (read_buffer_.length() > 0) {
      file_event_->activate(Event::FileReadyType::Read);
    }
  }
}

void ConnectionImpl::raiseEvent(ConnectionEvent event) {
  for (ConnectionCallbacks* callback : callbacks_) {
    // TODO(mattklein123): If we close while raising a connected event we should not raise further
    // connected events.
    callback->onEvent(event);
  }
  // We may have pending data in the write buffer on transport handshake
  // completion, which may also have completed in the context of onReadReady(),
  // where no check of the write buffer is made. Provide an opportunity to flush
  // here. If connection write is not ready, this is harmless. We should only do
  // this if we're still open (the above callbacks may have closed).
  if (state() == State::Open && event == ConnectionEvent::Connected &&
      write_buffer_->length() > 0) {
    onWriteReady();
  }
}

bool ConnectionImpl::readEnabled() const { return read_enabled_; }

void ConnectionImpl::addConnectionCallbacks(ConnectionCallbacks& cb) { callbacks_.push_back(&cb); }

void ConnectionImpl::addBytesSentCallback(BytesSentCb cb) {
  bytes_sent_callbacks_.emplace_back(cb);
}

void ConnectionImpl::write(Buffer::Instance& data, bool end_stream) {
  ASSERT(!end_stream || enable_half_close_);

  if (write_end_stream_) {
    // It is an API violation to write more data after writing end_stream, but a duplicate
    // end_stream with no data is harmless. This catches misuse of the API that could result in data
    // being lost.
    ASSERT(data.length() == 0 && end_stream);

    return;
  }

  // NOTE: This is kind of a hack, but currently we don't support restart/continue on the write
  //       path, so we just pass around the buffer passed to us in this function. If we ever support
  //       buffer/restart/continue on the write path this needs to get more complicated.
  current_write_buffer_ = &data;
  current_write_end_stream_ = end_stream;
  FilterStatus status = filter_manager_.onWrite();
  current_write_buffer_ = nullptr;

  if (FilterStatus::StopIteration == status) {
    return;
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
    // doWriteReady into thinking the socket is connected. On OS X, the underlying write may fail
    // with a connection error if a call to write(2) occurs before the connection is completed.
    if (!connecting_) {
      file_event_->activate(Event::FileReadyType::Write);
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
  }
}

void ConnectionImpl::onLowWatermark() {
  ENVOY_CONN_LOG(debug, "onBelowWriteBufferLowWatermark", *this);
  ASSERT(above_high_watermark_);
  above_high_watermark_ = false;
  for (ConnectionCallbacks* callback : callbacks_) {
    callback->onBelowWriteBufferLowWatermark();
  }
}

void ConnectionImpl::onHighWatermark() {
  ENVOY_CONN_LOG(debug, "onAboveWriteBufferHighWatermark", *this);
  ASSERT(!above_high_watermark_);
  above_high_watermark_ = true;
  for (ConnectionCallbacks* callback : callbacks_) {
    callback->onAboveWriteBufferHighWatermark();
  }
}

void ConnectionImpl::onFileEvent(uint32_t events) {
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
  if (fd() != -1 && (events & Event::FileReadyType::Read)) {
    onReadReady();
  }
}

void ConnectionImpl::onReadReady() {
  ENVOY_CONN_LOG(trace, "read ready", *this);

  ASSERT(!connecting_);

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
  if (result.bytes_processed_ != 0 || result.end_stream_read_) {
    // Skip onRead if no bytes were processed. For instance, if the connection was closed without
    // producing more data.
    onRead(new_buffer_size);
  }

  // The read callback may have already closed the connection.
  if (result.action_ == PostIoAction::Close || bothSidesHalfClosed()) {
    ENVOY_CONN_LOG(debug, "remote close", *this);
    closeSocket(ConnectionEvent::RemoteClose);
  }
}

void ConnectionImpl::onWriteReady() {
  ENVOY_CONN_LOG(trace, "write ready", *this);

  if (connecting_) {
    int error;
    socklen_t error_size = sizeof(error);
    int rc = getsockopt(fd(), SOL_SOCKET, SO_ERROR, &error, &error_size);
    ASSERT(0 == rc);

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

  if (result.action_ == PostIoAction::Close) {
    // It is possible (though unlikely) for the connection to have already been closed during the
    // write callback. This can happen if we manage to complete the SSL handshake in the write
    // callback, raise a connected event, and close the connection.
    closeSocket(ConnectionEvent::RemoteClose);
  } else if ((close_after_flush_ && new_buffer_size == 0) || bothSidesHalfClosed()) {
    ENVOY_CONN_LOG(debug, "write flush complete", *this);
    closeSocket(ConnectionEvent::LocalClose);
  } else if (result.action_ == PostIoAction::KeepOpen && result.bytes_processed_ > 0) {
    for (BytesSentCb& cb : bytes_sent_callbacks_) {
      cb(result.bytes_processed_);

      // If a callback closes the socket, stop iterating.
      if (fd() == -1) {
        return;
      }
    }
  }
}

void ConnectionImpl::setConnectionStats(const ConnectionStats& stats) {
  ASSERT(!connection_stats_,
         "Two network filters are attempting to set connection stats. This indicates an issue "
         "with the configured filter chain.");
  connection_stats_.reset(new ConnectionStats(stats));
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

void ConnectionImpl::onDelayedCloseTimeout() {
  ENVOY_CONN_LOG(debug, "triggered delayed close", *this);
  if (connection_stats_->delayed_close_timeouts_ != nullptr) {
    connection_stats_->delayed_close_timeouts_->inc();
  }
  closeSocket(ConnectionEvent::LocalClose);
}

ClientConnectionImpl::ClientConnectionImpl(
    Event::Dispatcher& dispatcher, const Address::InstanceConstSharedPtr& remote_address,
    const Network::Address::InstanceConstSharedPtr& source_address,
    Network::TransportSocketPtr&& transport_socket,
    const Network::ConnectionSocket::OptionsSharedPtr& options)
    : ConnectionImpl(dispatcher, std::make_unique<ClientSocketImpl>(remote_address),
                     std::move(transport_socket), false) {
  // There are no meaningful socket options or source address semantics for
  // non-IP sockets, so skip.
  if (remote_address->ip() != nullptr) {
    if (!Network::Socket::applyOptions(options, *socket_,
                                       envoy::api::v2::core::SocketOption::STATE_PREBIND)) {
      // Set a special error state to ensure asynchronous close to give the owner of the
      // ConnectionImpl a chance to add callbacks and detect the "disconnect".
      immediate_error_event_ = ConnectionEvent::LocalClose;
      // Trigger a write event to close this connection out-of-band.
      file_event_->activate(Event::FileReadyType::Write);
      return;
    }

    if (source_address != nullptr) {
      const Api::SysCallIntResult result = source_address->bind(fd());
      if (result.rc_ < 0) {
        ENVOY_LOG_MISC(debug, "Bind failure. Failed to bind to {}: {}", source_address->asString(),
                       strerror(result.errno_));
        bind_error_ = true;
        // Set a special error state to ensure asynchronous close to give the owner of the
        // ConnectionImpl a chance to add callbacks and detect the "disconnect".
        immediate_error_event_ = ConnectionEvent::LocalClose;

        // Trigger a write event to close this connection out-of-band.
        file_event_->activate(Event::FileReadyType::Write);
      }
    }
  }
}

void ClientConnectionImpl::connect() {
  ENVOY_CONN_LOG(debug, "connecting to {}", *this, socket_->remoteAddress()->asString());
  const Api::SysCallIntResult result = socket_->remoteAddress()->connect(fd());
  if (result.rc_ == 0) {
    // write will become ready.
    ASSERT(connecting_);
  } else {
    ASSERT(result.rc_ == -1);
    if (result.errno_ == EINPROGRESS) {
      ASSERT(connecting_);
      ENVOY_CONN_LOG(debug, "connection in progress", *this);
    } else {
      immediate_error_event_ = ConnectionEvent::RemoteClose;
      connecting_ = false;
      ENVOY_CONN_LOG(debug, "immediate connection error: {}", *this, result.errno_);

      // Trigger a write event. This is needed on OSX and seems harmless on Linux.
      file_event_->activate(Event::FileReadyType::Write);
    }
  }

  // The local address can only be retrieved for IP connections. Other
  // types, such as UDS, don't have a notion of a local address.
  if (socket_->remoteAddress()->type() == Address::Type::Ip) {
    socket_->setLocalAddress(Address::addressFromFd(fd()), false);
  }
}

} // namespace Network
} // namespace Envoy
