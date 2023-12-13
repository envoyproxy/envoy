#include "source/common/io/io_uring_worker_impl.h"

namespace Envoy {
namespace Io {

ReadRequest::ReadRequest(IoUringSocket& socket, uint32_t size)
    : Request(RequestType::Read, socket), buf_(std::make_unique<uint8_t[]>(size)),
      iov_(std::make_unique<struct iovec>()) {
  iov_->iov_base = buf_.get();
  iov_->iov_len = size;
}

WriteRequest::WriteRequest(IoUringSocket& socket, const Buffer::RawSliceVector& slices)
    : Request(RequestType::Write, socket), iov_(std::make_unique<struct iovec[]>(slices.size())) {
  for (size_t i = 0; i < slices.size(); i++) {
    iov_[i].iov_base = slices[i].mem_;
    iov_[i].iov_len = slices[i].len_;
  }
}

IoUringSocketEntry::IoUringSocketEntry(os_fd_t fd, IoUringWorkerImpl& parent, Event::FileReadyCb cb,
                                       bool enable_close_event)
    : fd_(fd), parent_(parent), enable_close_event_(enable_close_event), cb_(std::move(cb)) {}

void IoUringSocketEntry::cleanup() {
  IoUringSocketEntryPtr socket = parent_.removeSocket(*this);
  parent_.dispatcher().deferredDelete(std::move(socket));
}

void IoUringSocketEntry::injectCompletion(Request::RequestType type) {
  // Avoid injecting the same completion type multiple times.
  if (injected_completions_ & static_cast<uint8_t>(type)) {
    ENVOY_LOG(trace,
              "ignore injected completion since there already has one, injected_completions_: {}, "
              "type: {}",
              injected_completions_, static_cast<uint8_t>(type));
    return;
  }
  injected_completions_ |= static_cast<uint8_t>(type);
  parent_.injectCompletion(*this, type, -EAGAIN);
}

void IoUringSocketEntry::onReadCompleted() {
  ENVOY_LOG(trace,
            "calling event callback since pending read buf has {} size data, data = {}, "
            "fd = {}",
            getReadParam()->buf_.length(), getReadParam()->buf_.toString(), fd_);
  cb_(Event::FileReadyType::Read);
}

void IoUringSocketEntry::onWriteCompleted() {
  ENVOY_LOG(trace, "call event callback for write since result = {}", getWriteParam()->result_);
  cb_(Event::FileReadyType::Write);
}

void IoUringSocketEntry::onRemoteClose() {
  ENVOY_LOG(trace, "onRemoteClose fd = {}", fd_);
  cb_(Event::FileReadyType::Closed);
}

IoUringWorkerImpl::IoUringWorkerImpl(uint32_t io_uring_size, bool use_submission_queue_polling,
                                     uint32_t read_buffer_size, uint32_t write_timeout_ms,
                                     Event::Dispatcher& dispatcher)
    : IoUringWorkerImpl(std::make_unique<IoUringImpl>(io_uring_size, use_submission_queue_polling),
                        read_buffer_size, write_timeout_ms, dispatcher) {}

IoUringWorkerImpl::IoUringWorkerImpl(IoUringPtr&& io_uring, uint32_t read_buffer_size,
                                     uint32_t write_timeout_ms, Event::Dispatcher& dispatcher)
    : io_uring_(std::move(io_uring)), read_buffer_size_(read_buffer_size),
      write_timeout_ms_(write_timeout_ms), dispatcher_(dispatcher) {
  const os_fd_t event_fd = io_uring_->registerEventfd();
  // We only care about the read event of Eventfd, since we only receive the
  // event here.
  file_event_ = dispatcher_.createFileEvent(
      event_fd, [this](uint32_t) { onFileEvent(); }, Event::PlatformDefaultTriggerType,
      Event::FileReadyType::Read);
}

IoUringWorkerImpl::~IoUringWorkerImpl() {
  ENVOY_LOG(trace, "destruct io uring worker, existing sockets = {}", sockets_.size());

  for (auto& socket : sockets_) {
    if (socket->getStatus() != Closed) {
      socket->close(false);
    }
  }

  while (!sockets_.empty()) {
    ENVOY_LOG(trace, "still left {} sockets are not closed", sockets_.size());
    for (auto& socket : sockets_) {
      ENVOY_LOG(trace, "the socket fd = {} not closed", socket->fd());
    }
    onFileEvent();
  }

  dispatcher_.clearDeferredDeleteList();
}

IoUringSocket& IoUringWorkerImpl::addServerSocket(os_fd_t fd, Event::FileReadyCb cb,
                                                  bool enable_close_event) {
  ENVOY_LOG(trace, "add server socket, fd = {}", fd);
  std::unique_ptr<IoUringServerSocket> socket = std::make_unique<IoUringServerSocket>(
      fd, *this, std::move(cb), write_timeout_ms_, enable_close_event);
  socket->enableRead();
  return addSocket(std::move(socket));
}

IoUringSocket& IoUringWorkerImpl::addServerSocket(os_fd_t fd, Buffer::Instance& read_buf,
                                                  Event::FileReadyCb cb, bool enable_close_event) {
  ENVOY_LOG(trace, "add server socket through existing socket, fd = {}", fd);
  std::unique_ptr<IoUringServerSocket> socket = std::make_unique<IoUringServerSocket>(
      fd, read_buf, *this, std::move(cb), write_timeout_ms_, enable_close_event);
  socket->enableRead();
  return addSocket(std::move(socket));
}

Event::Dispatcher& IoUringWorkerImpl::dispatcher() { return dispatcher_; }

IoUringSocketEntry& IoUringWorkerImpl::addSocket(IoUringSocketEntryPtr&& socket) {
  LinkedList::moveIntoListBack(std::move(socket), sockets_);
  return *sockets_.back();
}

Request* IoUringWorkerImpl::submitReadRequest(IoUringSocket& socket) {
  ReadRequest* req = new ReadRequest(socket, read_buffer_size_);

  ENVOY_LOG(trace, "submit read request, fd = {}, read req = {}", socket.fd(), fmt::ptr(req));

  auto res = io_uring_->prepareReadv(socket.fd(), req->iov_.get(), 1, 0, req);
  if (res == IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    submit();
    res = io_uring_->prepareReadv(socket.fd(), req->iov_.get(), 1, 0, req);
    RELEASE_ASSERT(res == IoUringResult::Ok, "unable to prepare readv");
  }
  submit();
  return req;
}

Request* IoUringWorkerImpl::submitWriteRequest(IoUringSocket& socket,
                                               const Buffer::RawSliceVector& slices) {
  WriteRequest* req = new WriteRequest(socket, slices);

  ENVOY_LOG(trace, "submit write request, fd = {}, req = {}", socket.fd(), fmt::ptr(req));

  auto res = io_uring_->prepareWritev(socket.fd(), req->iov_.get(), slices.size(), 0, req);
  if (res == IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    submit();
    res = io_uring_->prepareWritev(socket.fd(), req->iov_.get(), slices.size(), 0, req);
    RELEASE_ASSERT(res == IoUringResult::Ok, "unable to prepare writev");
  }
  submit();
  return req;
}

Request* IoUringWorkerImpl::submitCloseRequest(IoUringSocket& socket) {
  Request* req = new Request(Request::RequestType::Close, socket);

  ENVOY_LOG(trace, "submit close request, fd = {}, close req = {}", socket.fd(), fmt::ptr(req));

  auto res = io_uring_->prepareClose(socket.fd(), req);
  if (res == IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    submit();
    res = io_uring_->prepareClose(socket.fd(), req);
    RELEASE_ASSERT(res == IoUringResult::Ok, "unable to prepare close");
  }
  submit();
  return req;
}

Request* IoUringWorkerImpl::submitCancelRequest(IoUringSocket& socket, Request* request_to_cancel) {
  Request* req = new Request(Request::RequestType::Cancel, socket);

  ENVOY_LOG(trace, "submit cancel request, fd = {}, cancel req = {}, req to cancel = {}",
            socket.fd(), fmt::ptr(req), fmt::ptr(request_to_cancel));

  auto res = io_uring_->prepareCancel(request_to_cancel, req);
  if (res == IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    submit();
    res = io_uring_->prepareCancel(request_to_cancel, req);
    RELEASE_ASSERT(res == IoUringResult::Ok, "unable to prepare cancel");
  }
  submit();
  return req;
}

Request* IoUringWorkerImpl::submitShutdownRequest(IoUringSocket& socket, int how) {
  Request* req = new Request(Request::RequestType::Shutdown, socket);

  ENVOY_LOG(trace, "submit shutdown request, fd = {}, shutdown req = {}", socket.fd(),
            fmt::ptr(req));

  auto res = io_uring_->prepareShutdown(socket.fd(), how, req);
  if (res == IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    submit();
    res = io_uring_->prepareShutdown(socket.fd(), how, req);
    RELEASE_ASSERT(res == IoUringResult::Ok, "unable to prepare cancel");
  }
  submit();
  return req;
}

IoUringSocketEntryPtr IoUringWorkerImpl::removeSocket(IoUringSocketEntry& socket) {
  // Remove all the injection completion for this socket.
  io_uring_->removeInjectedCompletion(socket.fd());
  return socket.removeFromList(sockets_);
}

void IoUringWorkerImpl::injectCompletion(IoUringSocket& socket, Request::RequestType type,
                                         int32_t result) {
  Request* req = new Request(type, socket);
  io_uring_->injectCompletion(socket.fd(), req, result);
  file_event_->activate(Event::FileReadyType::Read);
}

void IoUringWorkerImpl::onFileEvent() {
  ENVOY_LOG(trace, "io uring worker, on file event");
  delay_submit_ = true;
  io_uring_->forEveryCompletion([](Request* req, int32_t result, bool injected) {
    ENVOY_LOG(trace, "receive request completion, type = {}, req = {}",
              static_cast<uint8_t>(req->type()), fmt::ptr(req));
    ASSERT(req != nullptr);

    switch (req->type()) {
    case Request::RequestType::Accept:
      ENVOY_LOG(trace, "receive accept request completion, fd = {}, req = {}", req->socket().fd(),
                fmt::ptr(req));
      req->socket().onAccept(req, result, injected);
      break;
    case Request::RequestType::Connect:
      ENVOY_LOG(trace, "receive connect request completion, fd = {}, req = {}", req->socket().fd(),
                fmt::ptr(req));
      req->socket().onConnect(req, result, injected);
      break;
    case Request::RequestType::Read:
      ENVOY_LOG(trace, "receive Read request completion, fd = {}, req = {}", req->socket().fd(),
                fmt::ptr(req));
      req->socket().onRead(req, result, injected);
      break;
    case Request::RequestType::Write:
      ENVOY_LOG(trace, "receive write request completion, fd = {}, req = {}", req->socket().fd(),
                fmt::ptr(req));
      req->socket().onWrite(req, result, injected);
      break;
    case Request::RequestType::Close:
      ENVOY_LOG(trace, "receive close request completion, fd = {}, req = {}", req->socket().fd(),
                fmt::ptr(req));
      req->socket().onClose(req, result, injected);
      break;
    case Request::RequestType::Cancel:
      ENVOY_LOG(trace, "receive cancel request completion, fd = {}, req = {}", req->socket().fd(),
                fmt::ptr(req));
      req->socket().onCancel(req, result, injected);
      break;
    case Request::RequestType::Shutdown:
      ENVOY_LOG(trace, "receive shutdown request completion, fd = {}, req = {}", req->socket().fd(),
                fmt::ptr(req));
      req->socket().onShutdown(req, result, injected);
      break;
    }

    delete req;
  });
  delay_submit_ = false;
  submit();
}

void IoUringWorkerImpl::submit() {
  if (!delay_submit_) {
    io_uring_->submit();
  }
}

IoUringServerSocket::IoUringServerSocket(os_fd_t fd, IoUringWorkerImpl& parent,
                                         Event::FileReadyCb cb, uint32_t write_timeout_ms,
                                         bool enable_close_event)
    : IoUringSocketEntry(fd, parent, std::move(cb), enable_close_event),
      write_timeout_ms_(write_timeout_ms) {}

IoUringServerSocket::IoUringServerSocket(os_fd_t fd, Buffer::Instance& read_buf,
                                         IoUringWorkerImpl& parent, Event::FileReadyCb cb,
                                         uint32_t write_timeout_ms, bool enable_close_event)
    : IoUringSocketEntry(fd, parent, std::move(cb), enable_close_event),
      write_timeout_ms_(write_timeout_ms) {
  read_buf_.move(read_buf);
}

IoUringServerSocket::~IoUringServerSocket() {
  if (write_timeout_timer_) {
    write_timeout_timer_->disableTimer();
  }
}

void IoUringServerSocket::close(bool keep_fd_open, IoUringSocketOnClosedCb cb) {
  ENVOY_LOG(trace, "close the socket, fd = {}, status = {}", fd_, status_);

  IoUringSocketEntry::close(keep_fd_open, cb);
  keep_fd_open_ = keep_fd_open;

  // Delay close until read request and write (or shutdown) request are drained.
  if (read_req_ == nullptr && write_or_shutdown_req_ == nullptr) {
    closeInternal();
    return;
  }

  if (read_req_ != nullptr) {
    ENVOY_LOG(trace, "cancel the read request, fd = {}", fd_);
    read_cancel_req_ = parent_.submitCancelRequest(*this, read_req_);
  }

  if (write_or_shutdown_req_ != nullptr) {
    ENVOY_LOG(trace, "delay cancel the write request, fd = {}", fd_);
    if (write_timeout_ms_ > 0) {
      write_timeout_timer_ = parent_.dispatcher().createTimer([this]() {
        if (write_or_shutdown_req_ != nullptr) {
          ENVOY_LOG(trace, "cancel the write or shutdown request, fd = {}", fd_);
          write_or_shutdown_cancel_req_ =
              parent_.submitCancelRequest(*this, write_or_shutdown_req_);
        }
      });
      write_timeout_timer_->enableTimer(std::chrono::milliseconds(write_timeout_ms_));
    }
  }
}

void IoUringServerSocket::enableRead() {
  IoUringSocketEntry::enableRead();
  ENVOY_LOG(trace, "enable read, fd = {}", fd_);

  // Continue processing read buffer remained by the previous read.
  if (read_buf_.length() > 0 || read_error_.has_value()) {
    ENVOY_LOG(trace, "continue reading from socket, fd = {}, size = {}", fd_, read_buf_.length());
    injectCompletion(Request::RequestType::Read);
    return;
  }

  submitReadRequest();
}

void IoUringServerSocket::disableRead() { IoUringSocketEntry::disableRead(); }

void IoUringServerSocket::write(Buffer::Instance& data) {
  ENVOY_LOG(trace, "write, buffer size = {}, fd = {}", data.length(), fd_);
  ASSERT(!shutdown_.has_value());

  // We need to reset the drain trackers, since the write and close is async in
  // the iouring. When the write is actually finished the above layer may already
  // release the drain trackers.
  write_buf_.move(data, data.length(), true);

  submitWriteOrShutdownRequest();
}

uint64_t IoUringServerSocket::write(const Buffer::RawSlice* slices, uint64_t num_slice) {
  ENVOY_LOG(trace, "write, num_slices = {}, fd = {}", num_slice, fd_);
  ASSERT(!shutdown_.has_value());

  uint64_t bytes_written = 0;
  for (uint64_t i = 0; i < num_slice; i++) {
    write_buf_.add(slices[i].mem_, slices[i].len_);
    bytes_written += slices[i].len_;
  }

  submitWriteOrShutdownRequest();
  return bytes_written;
}

void IoUringServerSocket::shutdown(int how) {
  ENVOY_LOG(trace, "shutdown the socket, fd = {}, how = {}", fd_, how);
  ASSERT(how == SHUT_WR);
  shutdown_ = false;
  submitWriteOrShutdownRequest();
}

void IoUringServerSocket::onClose(Request* req, int32_t result, bool injected) {
  IoUringSocketEntry::onClose(req, result, injected);
  ASSERT(!injected);
  cleanup();
}

void IoUringServerSocket::onCancel(Request* req, int32_t result, bool injected) {
  IoUringSocketEntry::onCancel(req, result, injected);
  ASSERT(!injected);
  if (read_cancel_req_ == req) {
    read_cancel_req_ = nullptr;
  }
  if (write_or_shutdown_cancel_req_ == req) {
    write_or_shutdown_cancel_req_ = nullptr;
  }
  if (status_ == Closed && write_or_shutdown_req_ == nullptr && read_req_ == nullptr &&
      write_or_shutdown_cancel_req_ == nullptr) {
    closeInternal();
  }
}

void IoUringServerSocket::moveReadDataToBuffer(Request* req, size_t data_length) {
  ReadRequest* read_req = static_cast<ReadRequest*>(req);
  Buffer::BufferFragment* fragment = new Buffer::BufferFragmentImpl(
      read_req->buf_.release(), data_length,
      [](const void* data, size_t, const Buffer::BufferFragmentImpl* this_fragment) {
        delete[] reinterpret_cast<const uint8_t*>(data);
        delete this_fragment;
      });
  read_buf_.addBufferFragment(*fragment);
}

void IoUringServerSocket::onReadCompleted(int32_t result) {
  ENVOY_LOG(trace, "read from socket, fd = {}, result = {}", fd_, result);
  ReadParam param{read_buf_, result};
  read_param_ = param;
  IoUringSocketEntry::onReadCompleted();
  read_param_ = absl::nullopt;
  ENVOY_LOG(trace, "after read from socket, fd = {}, remain = {}", fd_, read_buf_.length());
}

// TODO(zhxie): concern submit multiple read requests or submit read request in advance to improve
// performance in the next iteration.
void IoUringServerSocket::onRead(Request* req, int32_t result, bool injected) {
  IoUringSocketEntry::onRead(req, result, injected);

  ENVOY_LOG(trace,
            "onRead with result {}, fd = {}, injected = {}, status_ = {}, enable_close_event = {}",
            result, fd_, injected, status_, enable_close_event_);
  if (!injected) {
    read_req_ = nullptr;
    // If the socket is going to close, discard all results.
    if (status_ == Closed && write_or_shutdown_req_ == nullptr && read_cancel_req_ == nullptr &&
        write_or_shutdown_cancel_req_ == nullptr) {
      if (result > 0 && keep_fd_open_) {
        moveReadDataToBuffer(req, result);
      }
      closeInternal();
      return;
    }
  }

  // Move read data from request to buffer or store the error.
  if (result > 0) {
    moveReadDataToBuffer(req, result);
  } else {
    if (result != -ECANCELED) {
      read_error_ = result;
    }
  }

  // Discard calling back since the socket is not ready or closed.
  if (status_ == Initialized || status_ == Closed) {
    return;
  }

  // If the socket is enabled and there are bytes to read, notify the handler.
  if (status_ == ReadEnabled) {
    if (read_buf_.length() > 0) {
      onReadCompleted(static_cast<int32_t>(read_buf_.length()));
    } else if (read_error_.has_value() && read_error_ < 0) {
      onReadCompleted(read_error_.value());
      read_error_.reset();
    }
    // Handle remote closed at last.
    // Depending on the event listened to, calling different event back to handle remote closed.
    // * events & (Read | Closed): Callback Closed,
    // * events & (Read)         : Callback Read,
    // * events & (Closed)       : Callback Closed,
    // * ...else                 : Callback Write.
    if (read_error_.has_value() && read_error_ == 0 && !enable_close_event_) {
      ENVOY_LOG(trace, "read remote closed from socket, fd = {}", fd_);
      onReadCompleted(read_error_.value());
      read_error_.reset();
      return;
    }
  }

  // If `enable_close_event_` is true, then deliver the remote close as close event.
  if (read_error_.has_value() && read_error_ == 0) {
    if (enable_close_event_) {
      ENVOY_LOG(trace,
                "remote closed and close event enabled, raise the close event, fd = "
                "{}, result = {}",
                fd_, read_error_.value());
      status_ = RemoteClosed;
      IoUringSocketEntry::onRemoteClose();
      read_error_.reset();
      return;
    } else {
      // In this case, the closed event isn't listened and the status is disabled.
      // It means we can't raise the closed or read event. So we only can raise the
      // write event.
      ENVOY_LOG(trace,
                "remote closed and close event disabled, raise the write event, fd = "
                "{}, result = {}",
                fd_, read_error_.value());
      status_ = RemoteClosed;
      onWriteCompleted(0);
      read_error_.reset();
      return;
    }
  }

  // The socket may be not readable during handler onRead callback, check it again here.
  if (status_ == ReadEnabled) {
    // If the read error is zero, it means remote close, then needn't new request.
    if (!read_error_.has_value() || read_error_.value() != 0) {
      // Submit a read request for the next read.
      submitReadRequest();
    }
  } else if (status_ == ReadDisabled) {
    // Since error in a disabled socket will not be handled by the handler, stop submit read
    // request if there is any error.
    if (!read_error_.has_value()) {
      // Submit a read request for monitoring the remote close event, otherwise there is no
      // way to know the connection is closed by the remote.
      submitReadRequest();
    }
  }
}

void IoUringServerSocket::onWriteCompleted(int32_t result) {
  WriteParam param{result};
  write_param_ = param;
  IoUringSocketEntry::onWriteCompleted();
  write_param_ = absl::nullopt;
}

void IoUringServerSocket::onWrite(Request* req, int32_t result, bool injected) {
  IoUringSocketEntry::onWrite(req, result, injected);

  ENVOY_LOG(trace, "onWrite with result {}, fd = {}, injected = {}, status_ = {}", result, fd_,
            injected, status_);
  if (!injected) {
    write_or_shutdown_req_ = nullptr;
  }

  // Notify the handler directly since it is an injected request.
  if (injected) {
    ENVOY_LOG(trace,
              "there is a inject event, and same time we have regular write request, fd = {}", fd_);
    // There is case where write injection may come after shutdown or close which should be ignored
    // since the I/O handle or connection may be released after closing.
    if (!shutdown_.has_value() && status_ != Closed) {
      onWriteCompleted(result);
    }
    return;
  }

  if (result > 0) {
    write_buf_.drain(result);
    ENVOY_LOG(trace, "drain write buf, drain size = {}, fd = {}", result, fd_);
  } else {
    // Drain all write buf since the write failed.
    write_buf_.drain(write_buf_.length());
    if (!shutdown_.has_value() && status_ != Closed) {
      status_ = RemoteClosed;
      if (result == -EPIPE) {
        IoUringSocketEntry::onRemoteClose();
      } else {
        onWriteCompleted(result);
      }
    }
  }

  submitWriteOrShutdownRequest();
}

void IoUringServerSocket::onShutdown(Request* req, int32_t result, bool injected) {
  IoUringSocketEntry::onShutdown(req, result, injected);

  ENVOY_LOG(trace, "onShutdown with result {}, fd = {}, injected = {}", result, fd_, injected);
  ASSERT(!injected);
  write_or_shutdown_req_ = nullptr;
  shutdown_ = true;

  submitWriteOrShutdownRequest();
}

void IoUringServerSocket::closeInternal() {
  if (keep_fd_open_) {
    if (on_closed_cb_) {
      on_closed_cb_(read_buf_);
    }
    cleanup();
    return;
  }
  if (close_req_ == nullptr) {
    close_req_ = parent_.submitCloseRequest(*this);
  }
}

void IoUringServerSocket::submitReadRequest() {
  if (!read_req_) {
    read_req_ = parent_.submitReadRequest(*this);
  }
}

void IoUringServerSocket::submitWriteOrShutdownRequest() {
  if (!write_or_shutdown_req_) {
    if (write_buf_.length() > 0) {
      Buffer::RawSliceVector slices = write_buf_.getRawSlices(IOV_MAX);
      ENVOY_LOG(trace, "submit write request, write_buf size = {}, num_iovecs = {}, fd = {}",
                write_buf_.length(), slices.size(), fd_);
      write_or_shutdown_req_ = parent_.submitWriteRequest(*this, slices);
    } else if (shutdown_.has_value() && !shutdown_.value()) {
      write_or_shutdown_req_ = parent_.submitShutdownRequest(*this, SHUT_WR);
    } else if (status_ == Closed && read_req_ == nullptr && read_cancel_req_ == nullptr &&
               write_or_shutdown_cancel_req_ == nullptr) {
      closeInternal();
    }
  }
}

} // namespace Io
} // namespace Envoy
