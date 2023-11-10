#include "source/common/io/io_uring_worker_impl.h"

#include <sys/socket.h>

namespace Envoy {
namespace Io {

AcceptRequest::AcceptRequest(IoUringSocket& socket) : Request(RequestType::Accept, socket) {}

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
              "ignore injected completion since there already has one, injected_completions_: {}, ",
              injected_completions_);
    return;
  }
  injected_completions_ |= static_cast<uint8_t>(type);
  parent_.injectCompletion(*this, type, -EAGAIN);
}

void IoUringSocketEntry::onAcceptCompleted() {
  ENVOY_LOG(trace, "before on accept socket");
  cb_(Event::FileReadyType::Read);
  ENVOY_LOG(trace, "after on accept socket");
}

void IoUringSocketEntry::onReadCompleted() {
  ENVOY_LOG(trace,
            "calling event callback since pending read buf has {} size data, "
            "fd = {}",
            getReadParam()->buf_.length(), fd_);
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

void IoUringSocketEntry::onLocalClose() {
  ENVOY_LOG(trace, "onLocalClose fd = {}", fd_);
  // io_uring_socket_.reset();
}

IoUringWorkerImpl::IoUringWorkerImpl(uint32_t io_uring_size, bool use_submission_queue_polling,
                                     uint32_t accept_size, uint32_t read_buffer_size,
                                     uint32_t write_timeout_ms, Event::Dispatcher& dispatcher)
    : IoUringWorkerImpl(std::make_unique<IoUringImpl>(io_uring_size, use_submission_queue_polling),
                        accept_size, read_buffer_size, write_timeout_ms, dispatcher) {}

IoUringWorkerImpl::IoUringWorkerImpl(std::unique_ptr<IoUring> io_uring, uint32_t accept_size,
                                     uint32_t read_buffer_size, uint32_t write_timeout_ms,
                                     Event::Dispatcher& dispatcher)
    : io_uring_(std::move(io_uring)), accept_size_(accept_size),
      read_buffer_size_(read_buffer_size), write_timeout_ms_(write_timeout_ms),
      dispatcher_(dispatcher) {
  const os_fd_t event_fd = io_uring_->registerEventfd();
  // We only care about the read event of Eventfd, since we only receive the
  // event here.
  file_event_ = dispatcher_.createFileEvent(
      event_fd, [this](uint32_t) { onFileEvent(); }, Event::PlatformDefaultTriggerType,
      Event::FileReadyType::Read);
}

IoUringWorkerImpl::~IoUringWorkerImpl() {
  ENVOY_LOG(trace, "destruct io uring worker, existed sockets = {}", sockets_.size());

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

IoUringSocket& IoUringWorkerImpl::addAcceptSocket(os_fd_t fd, Event::FileReadyCb cb,
                                                  bool enable_close_event) {
  ENVOY_LOG(trace, "add accept socket, fd = {}", fd);
  std::unique_ptr<IoUringAcceptSocket> socket = std::make_unique<IoUringAcceptSocket>(
      fd, *this, std::move(cb), accept_size_, enable_close_event);
  socket->enable();
  return addSocket(std::move(socket));
}

IoUringSocket& IoUringWorkerImpl::addServerSocket(os_fd_t fd, Event::FileReadyCb cb,
                                                  bool enable_close_event) {
  ENVOY_LOG(trace, "add server socket, fd = {}", fd);
  std::unique_ptr<IoUringServerSocket> socket = std::make_unique<IoUringServerSocket>(
      fd, *this, std::move(cb), write_timeout_ms_, enable_close_event);
  socket->enable();
  return addSocket(std::move(socket));
}

IoUringSocket& IoUringWorkerImpl::addServerSocket(os_fd_t fd, Buffer::Instance& read_buf,
                                                  Event::FileReadyCb cb, bool enable_close_event) {
  ENVOY_LOG(trace, "add server socket through existing socket, fd = {}", fd);
  std::unique_ptr<IoUringServerSocket> socket = std::make_unique<IoUringServerSocket>(
      fd, read_buf, *this, std::move(cb), write_timeout_ms_, enable_close_event);
  socket->enable();
  return addSocket(std::move(socket));
}

IoUringSocket& IoUringWorkerImpl::addClientSocket(os_fd_t fd, Event::FileReadyCb cb,
                                                  bool enable_close_event) {
  ENVOY_LOG(trace, "add client socket, fd = {}", fd);
  return addSocket(std::make_unique<IoUringClientSocket>(fd, *this, std::move(cb),
                                                         write_timeout_ms_, enable_close_event));
}

IoUringSocketEntry& IoUringWorkerImpl::addSocket(IoUringSocketEntryPtr&& socket) {
  LinkedList::moveIntoListBack(std::move(socket), sockets_);
  return *sockets_.back();
}

Event::Dispatcher& IoUringWorkerImpl::dispatcher() { return dispatcher_; }

Request* IoUringWorkerImpl::submitAcceptRequest(IoUringSocket& socket) {
  AcceptRequest* req = new AcceptRequest(socket);

  ENVOY_LOG(trace, "submit accept request, fd = {}, accept req = {}", socket.fd(), fmt::ptr(req));

  auto res =
      io_uring_->prepareAccept(socket.fd(), reinterpret_cast<struct sockaddr*>(&req->remote_addr_),
                               &req->remote_addr_len_, req);
  if (res == IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    submit();
    res = io_uring_->prepareAccept(socket.fd(),
                                   reinterpret_cast<struct sockaddr*>(&req->remote_addr_),
                                   &req->remote_addr_len_, req);
    RELEASE_ASSERT(res == IoUringResult::Ok, "unable to prepare accept");
  }
  submit();
  return req;
}

Request*
IoUringWorkerImpl::submitConnectRequest(IoUringSocket& socket,
                                        const Network::Address::InstanceConstSharedPtr& address) {
  Request* req = new Request(Request::RequestType::Connect, socket);

  ENVOY_LOG(trace, "submit connect request, fd = {}, req = {}", socket.fd(), fmt::ptr(req));

  auto res = io_uring_->prepareConnect(socket.fd(), address, req);
  if (res == IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    submit();
    res = io_uring_->prepareConnect(socket.fd(), address, req);
    RELEASE_ASSERT(res == IoUringResult::Ok, "unable to prepare writev");
  }
  submit();
  return req;
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

IoUringAcceptSocket::IoUringAcceptSocket(os_fd_t fd, IoUringWorkerImpl& parent,
                                         Event::FileReadyCb cb, uint32_t accept_size,
                                         bool enable_close_event)
    : IoUringSocketEntry(fd, parent, std::move(cb), enable_close_event), accept_size_(accept_size),
      requests_(std::vector<Request*>(accept_size_, nullptr)) {}

void IoUringAcceptSocket::close(bool keep_fd_open, IoUringSocketOnClosedCb cb) {
  IoUringSocketEntry::close(keep_fd_open, cb);
  close(keep_fd_open, cb, false);
}

void IoUringAcceptSocket::close(bool keep_fd_open, IoUringSocketOnClosedCb cb, bool posted) {
  // Ensure the close is done by the thread of socket running.
  // TODO (soulxu): This fixes the race problem, but it has performance penalty.
  // Thinking of a way to close the socket on its own thread in the listener.
  if (!posted) {
    parent_.dispatcher().post([this, keep_fd_open, cb]() { close(keep_fd_open, cb, true); });
    return;
  }

  ENVOY_LOG(trace, "close the socket, fd = {}, status = {}, request_count_ = {}, closed_ = {}", fd_,
            status_, request_count_, closed_);

  // We didn't implement keep_fd_open for accept socket.
  ASSERT(!keep_fd_open);

  // Delay close until all accept requests are drained.
  if (!request_count_) {
    if (!closed_) {
      closed_ = true;
      parent_.submitCloseRequest(*this);
    }
    return;
  }

  // TODO(soulxu): after kernel 5.19, we are able to cancel all requests for the specific fd.
  // TODO(zhxie): there may be races between main thread and worker thread in request_count_ and
  // requests_ in close(). When there is a listener draining, all server sockets in the listener
  // will be closed by the main thread. Though there may be races, it is still safe to
  // submitCancelRequest since io_uring can accept cancelling an invalid user_data.
  for (auto& req : requests_) {
    if (req != nullptr) {
      parent_.submitCancelRequest(*this, req);
      req = nullptr;
    }
  }
}

void IoUringAcceptSocket::enable() {
  IoUringSocketEntry::enable();
  submitRequests();
}

void IoUringAcceptSocket::disable() {
  IoUringSocketEntry::disable();
  // TODO (soulxu): after kernel 5.19, we are able to cancel all requests for the specific fd.
  for (auto& req : requests_) {
    if (req != nullptr) {
      parent_.submitCancelRequest(*this, req);
      req = nullptr;
    }
  }
}

void IoUringAcceptSocket::onClose(Request* req, int32_t result, bool injected) {
  IoUringSocketEntry::onClose(req, result, injected);
  ASSERT(!injected);
  cleanup();
}

void IoUringAcceptSocket::onAccept(Request* req, int32_t result, bool injected) {
  IoUringSocketEntry::onAccept(req, result, injected);
  ENVOY_LOG(trace,
            "onAccept with result {}, fd = {}, injected = {}, status_ = {}, request_count_ = {}",
            result, fd_, injected, status_, request_count_);
  ASSERT(!injected);
  AcceptRequest* accept_req = static_cast<AcceptRequest*>(req);
  requests_[accept_req->i_] = nullptr;
  request_count_--;
  // If there is no pending accept request and the socket is going to close, submit close request.
  if (status_ == Closed && !request_count_) {
    if (!closed_) {
      closed_ = true;
      parent_.submitCloseRequest(*this);
    }
  }

  // If the socket is not enabled, drop all following actions to all accepted fds.
  if (status_ == Enabled) {
    // Submit a new accept request for the next connection.
    submitRequests();
    if (result != -ECANCELED) {
      ENVOY_LOG(trace, "accept new socket, fd = {}, result = {}", fd_, result);
      AcceptedSocketParam param{result, &accept_req->remote_addr_, accept_req->remote_addr_len_};
      accepted_socket_param_ = param;
      onAcceptCompleted();
      accepted_socket_param_ = absl::nullopt;
    }
  }
}

void IoUringAcceptSocket::submitRequests() {
  for (size_t i = 0; i < requests_.size(); i++) {
    if (requests_[i] == nullptr) {
      requests_[i] = parent_.submitAcceptRequest(*this);
      static_cast<AcceptRequest*>(requests_[i])->i_ = i;
      request_count_++;
    }
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
    cancel_req_ = parent_.submitCancelRequest(*this, read_req_);
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

void IoUringServerSocket::enable() {
  IoUringSocketEntry::enable();
  ENVOY_LOG(trace, "enable, fd = {}", fd_);

  // Continue processing read buffer remained by the previous read.
  if (read_buf_.length() > 0 || read_error_.has_value()) {
    ENVOY_LOG(trace, "continue reading from socket, fd = {}, size = {}", fd_, read_buf_.length());
    injectCompletion(Request::RequestType::Read);
    return;
  }

  submitReadRequest();
}

void IoUringServerSocket::disable() { IoUringSocketEntry::disable(); }

void IoUringServerSocket::write(Buffer::Instance& data) {
  ENVOY_LOG(trace, "write, buffer size = {}, fd = {}", data.length(), fd_);
  ASSERT(!shutdown_.has_value());

  // We need to reset the drain trackers, since the write and close is async in
  // the io-uring. When the write is actually finished the above layer may already
  // release the drain trackers.
  // write_buf_.move(data, data.length(), true);

  // We still can't use the move. Since we still have many places use the
  // BufferFragment and release them by the drain tracker. This will lose
  // a little performance, let us figure out a solution in the future.
  write_buf_.add(data);
  data.drain(data.length());

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
  if (how != SHUT_WR) {
    PANIC("only the SHUT_WR implemented");
  }

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
  if (cancel_req_ == req) {
    cancel_req_ = nullptr;
  }
  if (write_or_shutdown_cancel_req_ == req) {
    write_or_shutdown_cancel_req_ = nullptr;
  }
  if (status_ == Closed && write_or_shutdown_req_ == nullptr && read_req_ == nullptr &&
      write_or_shutdown_cancel_req_ == nullptr) {
    closeInternal();
  }
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
    if (status_ == Closed && write_or_shutdown_req_ == nullptr && cancel_req_ == nullptr &&
        write_or_shutdown_cancel_req_ == nullptr) {
      if (result > 0 && keep_fd_open_) {
        ReadRequest* read_req = static_cast<ReadRequest*>(req);
        Buffer::BufferFragment* fragment = new Buffer::BufferFragmentImpl(
            read_req->buf_.release(), result,
            [](const void* data, size_t, const Buffer::BufferFragmentImpl* this_fragment) {
              delete[] reinterpret_cast<const uint8_t*>(data);
              delete this_fragment;
            });
        read_buf_.addBufferFragment(*fragment);
      }
      closeInternal();
      return;
    }
  }

  // Move read data from request to buffer or store the error.
  if (result > 0) {
    ReadRequest* read_req = static_cast<ReadRequest*>(req);
    Buffer::BufferFragment* fragment = new Buffer::BufferFragmentImpl(
        read_req->buf_.release(), result,
        [](const void* data, size_t, const Buffer::BufferFragmentImpl* this_fragment) {
          delete[] reinterpret_cast<const uint8_t*>(data);
          delete this_fragment;
        });
    read_buf_.addBufferFragment(*fragment);
  } else {
    if (result != -ECANCELED) {
      read_error_ = result;
    }
  }

  // Discard calling back since the socket is not ready or closed.
  if (status_ == Initialized || status_ == Closed) {
    return;
  }

  // If the socket is enabled and there is bytes to read, notify the handler.
  if (status_ == Enabled) {
    if (read_buf_.length() > 0) {
      ENVOY_LOG(trace, "read from socket, fd = {}, result = {}", fd_, read_buf_.length());
      ReadParam param{read_buf_, static_cast<int32_t>(read_buf_.length())};
      read_param_ = param;
      onReadCompleted();
      read_param_ = absl::nullopt;
      ENVOY_LOG(trace, "after read from socket, fd = {}, remain = {}", fd_, read_buf_.length());
    } else if (read_error_.has_value() && read_error_ < 0) {
      ENVOY_LOG(trace, "read error from socket, fd = {}, result = {}", fd_, read_error_.value());
      ReadParam param{read_buf_, read_error_.value()};
      read_param_ = param;
      onReadCompleted();
      read_param_ = absl::nullopt;
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
      ReadParam param{read_buf_, read_error_.value()};
      read_param_ = param;
      onReadCompleted();
      read_param_ = absl::nullopt;
      read_error_.reset();
      return;
    }
  }

  // If `enable_close_event_` is true, then deliver the remote close as close event.
  // TODO (soulxu): Add test for the case the status is enabled, but listen on the closed
  // event. This case is used for listener filter.
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
      // TODO (soulxu): We should try to move this logic to the
      // `IoUringSocketHandle::onRemoteClose()`.
      ENVOY_LOG(trace,
                "remote closed and close event disabled, raise the write event, fd = "
                "{}, result = {}",
                fd_, read_error_.value());
      status_ = RemoteClosed;
      WriteParam param{0};
      write_param_ = param;
      IoUringSocketEntry::onWriteCompleted();
      write_param_ = absl::nullopt;
      read_error_.reset();
      return;
    }
  }

  // The socket may be not readable during handler onRead callback, check it again here.
  if (status_ == Enabled) {
    // If the read error is zero, it means remote close, then needn't new request.
    if (!read_error_.has_value() || read_error_.value() != 0) {
      // Submit a read accept request for the next read.
      submitReadRequest();
    }
  } else if (status_ == Disabled) {
    // Since error in a disabled socket will not be handled by the handler, stop submit read
    // request if there is any error.
    if (!read_error_.has_value()) {
      // Submit a read accept request for the next read.
      submitReadRequest();
    }
  }
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
      WriteParam param{result};
      write_param_ = param;
      IoUringSocketEntry::onWriteCompleted();
      write_param_ = absl::nullopt;
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
      WriteParam param{result};
      write_param_ = param;
      if (result == -EPIPE) {
        IoUringSocketEntry::onRemoteClose();
      } else {
        IoUringSocketEntry::onWriteCompleted();
      }
      write_param_ = absl::nullopt;
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
      on_closed_cb_();
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
    } else if (status_ == Closed && read_req_ == nullptr && cancel_req_ == nullptr &&
               write_or_shutdown_cancel_req_ == nullptr) {
      closeInternal();
    }
  }
}

IoUringClientSocket::IoUringClientSocket(os_fd_t fd, IoUringWorkerImpl& parent,
                                         Event::FileReadyCb cb, uint32_t write_timeout_ms,
                                         bool enable_close_event)
    : IoUringServerSocket(fd, parent, cb, write_timeout_ms, enable_close_event) {}

void IoUringClientSocket::connect(const Network::Address::InstanceConstSharedPtr& address) {
  // Reuse read request since there is no read on connecting and connect is cancellable.
  ASSERT(read_req_ == nullptr);
  read_req_ = parent_.submitConnectRequest(*this, address);
}

void IoUringClientSocket::onConnect(Request* req, int32_t result, bool injected) {
  IoUringSocketEntry::onConnect(req, result, injected);
  ASSERT(!injected);
  ENVOY_LOG(trace, "onConnect with result {}, fd = {}, injected = {}, status_ = {}", result, fd_,
            injected, status_);

  read_req_ = nullptr;
  // Socket may be closed on connecting like binding error. In this situation we may not callback
  // on connecting completion.
  if (status_ == Closed) {
    if (write_or_shutdown_req_ == nullptr && cancel_req_ == nullptr &&
        write_or_shutdown_cancel_req_ == nullptr) {
      closeInternal();
    }
    return;
  }

  if (result == 0) {
    enable();
  }
  // Calls parent injectCompletion() directly since we want to send connect result back to the IO
  // handle.
  parent_.injectCompletion(*this, Request::RequestType::Write, result);
}

} // namespace Io
} // namespace Envoy
