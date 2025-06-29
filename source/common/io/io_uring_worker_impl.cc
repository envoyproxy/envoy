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

SendRequest::SendRequest(IoUringSocket& socket, const void* buf, size_t len, int flags)
    : Request(RequestType::Send, socket), buf_(buf), len_(len), flags_(flags) {}

RecvRequest::RecvRequest(IoUringSocket& socket, size_t len, int flags)
    : Request(RequestType::Recv, socket), buf_(std::make_unique<uint8_t[]>(len)), len_(len),
      flags_(flags) {}

SendMsgRequest::SendMsgRequest(IoUringSocket& socket, const struct msghdr* msg, int flags)
    : Request(RequestType::SendMsg, socket), msg_(std::make_unique<struct msghdr>()),
      flags_(flags) {
  // Deep copy the msghdr structure.
  *msg_ = *msg;

  // Copy iovec array.
  if (msg->msg_iovlen > 0) {
    iov_ = std::make_unique<struct iovec[]>(msg->msg_iovlen);
    for (size_t i = 0; i < msg->msg_iovlen; i++) {
      iov_[i] = msg->msg_iov[i];
    }
    msg_->msg_iov = iov_.get();
  }

  // Copy name buffer.
  if (msg->msg_name && msg->msg_namelen > 0) {
    name_buf_ = std::make_unique<uint8_t[]>(msg->msg_namelen);
    memcpy(name_buf_.get(), msg->msg_name, msg->msg_namelen); // NOLINT(safe-memcpy)
    msg_->msg_name = name_buf_.get();
  }

  // Copy control buffer.
  if (msg->msg_control && msg->msg_controllen > 0) {
    control_buf_ = std::make_unique<uint8_t[]>(msg->msg_controllen);
    memcpy(control_buf_.get(), msg->msg_control, msg->msg_controllen); // NOLINT(safe-memcpy)
    msg_->msg_control = control_buf_.get();
  }
}

RecvMsgRequest::RecvMsgRequest(IoUringSocket& socket, size_t buf_size, size_t control_size,
                               int flags)
    : Request(RequestType::RecvMsg, socket), msg_(std::make_unique<struct msghdr>()),
      iov_(std::make_unique<struct iovec>()), buf_(std::make_unique<uint8_t[]>(buf_size)),
      name_buf_(std::make_unique<uint8_t[]>(256)), // Standard sockaddr_storage size
      control_buf_(std::make_unique<uint8_t[]>(control_size)), buf_size_(buf_size),
      control_size_(control_size), flags_(flags) {
  // Setup iovec.
  iov_->iov_base = buf_.get();
  iov_->iov_len = buf_size;

  // Setup msghdr.
  memset(msg_.get(), 0, sizeof(struct msghdr));
  msg_->msg_name = name_buf_.get();
  msg_->msg_namelen = 256;
  msg_->msg_iov = iov_.get();
  msg_->msg_iovlen = 1;
  msg_->msg_control = control_buf_.get();
  msg_->msg_controllen = control_size;
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
  if (injected_completions_ & static_cast<uint16_t>(type)) {
    ENVOY_LOG(trace,
              "ignore injected completion since there already has one, injected_completions_: {}, "
              "type: {}",
              injected_completions_, static_cast<uint16_t>(type));
    return;
  }
  injected_completions_ |= static_cast<uint16_t>(type);
  parent_.injectCompletion(*this, type, -EAGAIN);
}

void IoUringSocketEntry::onReadCompleted() {
  ENVOY_LOG(trace,
            "calling event callback since pending read buf has {} size data, data = {}, "
            "fd = {}",
            getReadParam()->buf_.length(), getReadParam()->buf_.toString(), fd_);
  THROW_IF_NOT_OK(cb_(Event::FileReadyType::Read));
}

void IoUringSocketEntry::onWriteCompleted() {
  ENVOY_LOG(trace, "call event callback for write since result = {}", getWriteParam()->result_);
  THROW_IF_NOT_OK(cb_(Event::FileReadyType::Write));
}

void IoUringSocketEntry::onRemoteClose() {
  ENVOY_LOG(trace, "onRemoteClose fd = {}", fd_);
  THROW_IF_NOT_OK(cb_(Event::FileReadyType::Closed));
}

IoUringWorkerImpl::IoUringWorkerImpl(uint32_t io_uring_size, bool use_submission_queue_polling,
                                     uint32_t read_buffer_size, uint32_t write_timeout_ms,
                                     Event::Dispatcher& dispatcher, IoUringMode mode)
    : IoUringWorkerImpl(std::make_unique<IoUringImpl>(io_uring_size, use_submission_queue_polling),
                        read_buffer_size, write_timeout_ms, dispatcher, mode) {}

IoUringWorkerImpl::IoUringWorkerImpl(IoUringPtr&& io_uring, uint32_t read_buffer_size,
                                     uint32_t write_timeout_ms, Event::Dispatcher& dispatcher,
                                     IoUringMode mode)
    : io_uring_(std::move(io_uring)), read_buffer_size_(read_buffer_size),
      write_timeout_ms_(write_timeout_ms), dispatcher_(dispatcher), mode_(mode) {
  const os_fd_t event_fd = io_uring_->registerEventfd();
  // We only care about the read event of Eventfd, since we only receive the
  // event here.
  file_event_ = dispatcher_.createFileEvent(
      event_fd,
      [this](uint32_t) {
        onFileEvent();
        return absl::OkStatus();
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
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

IoUringSocket& IoUringWorkerImpl::addClientSocket(os_fd_t fd, Event::FileReadyCb cb,
                                                  bool enable_close_event) {
  ENVOY_LOG(trace, "add client socket, fd = {}", fd);
  // The client socket should not be read enabled until it is connected.
  std::unique_ptr<IoUringClientSocket> socket = std::make_unique<IoUringClientSocket>(
      fd, *this, std::move(cb), write_timeout_ms_, enable_close_event);
  return addSocket(std::move(socket));
}

Event::Dispatcher& IoUringWorkerImpl::dispatcher() { return dispatcher_; }

IoUringSocketEntry& IoUringWorkerImpl::addSocket(IoUringSocketEntryPtr&& socket) {
  LinkedList::moveIntoListBack(std::move(socket), sockets_);
  return *sockets_.back();
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
    RELEASE_ASSERT(res == IoUringResult::Ok, "unable to prepare connect");
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

Request* IoUringWorkerImpl::submitSendRequest(IoUringSocket& socket, const void* buf, size_t len,
                                              int flags) {
  SendRequest* req = new SendRequest(socket, buf, len, flags);

  ENVOY_LOG(trace, "submit send request, fd = {}, req = {}", socket.fd(), fmt::ptr(req));

  auto res = io_uring_->prepareSend(socket.fd(), req->buf_, req->len_, req->flags_, req);
  if (res == IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    submit();
    res = io_uring_->prepareSend(socket.fd(), req->buf_, req->len_, req->flags_, req);
    RELEASE_ASSERT(res == IoUringResult::Ok, "unable to prepare send");
  }
  submit();
  return req;
}

Request* IoUringWorkerImpl::submitRecvRequest(IoUringSocket& socket, void* buf, size_t len,
                                              int flags) {
  (void)buf; // Parameter not used - RecvRequest allocates its own buffer
  RecvRequest* req = new RecvRequest(socket, len, flags);

  ENVOY_LOG(trace, "submit recv request, fd = {}, req = {}", socket.fd(), fmt::ptr(req));

  auto res = io_uring_->prepareRecv(socket.fd(), req->buf_.get(), req->len_, req->flags_, req);
  if (res == IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    submit();
    res = io_uring_->prepareRecv(socket.fd(), req->buf_.get(), req->len_, req->flags_, req);
    RELEASE_ASSERT(res == IoUringResult::Ok, "unable to prepare recv");
  }
  submit();
  return req;
}

Request* IoUringWorkerImpl::submitSendmsgRequest(IoUringSocket& socket, const struct msghdr* msg,
                                                 int flags) {
  SendMsgRequest* req = new SendMsgRequest(socket, msg, flags);

  ENVOY_LOG(trace, "submit sendmsg request, fd = {}, req = {}", socket.fd(), fmt::ptr(req));

  auto res = io_uring_->prepareSendmsg(socket.fd(), req->msg_.get(), req->flags_, req);
  if (res == IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    submit();
    res = io_uring_->prepareSendmsg(socket.fd(), req->msg_.get(), req->flags_, req);
    RELEASE_ASSERT(res == IoUringResult::Ok, "unable to prepare sendmsg");
  }
  submit();
  return req;
}

Request* IoUringWorkerImpl::submitRecvmsgRequest(IoUringSocket& socket, struct msghdr* msg,
                                                 int flags) {
  // Extract size information from the provided msg structure.
  size_t buf_size = 0;
  for (size_t i = 0; i < msg->msg_iovlen; i++) {
    buf_size += msg->msg_iov[i].iov_len;
  }
  size_t control_size = msg->msg_controllen;

  RecvMsgRequest* req = new RecvMsgRequest(socket, buf_size, control_size, flags);

  ENVOY_LOG(trace, "submit recvmsg request, fd = {}, req = {}", socket.fd(), fmt::ptr(req));

  auto res = io_uring_->prepareRecvmsg(socket.fd(), req->msg_.get(), req->flags_, req);
  if (res == IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    submit();
    res = io_uring_->prepareRecvmsg(socket.fd(), req->msg_.get(), req->flags_, req);
    RELEASE_ASSERT(res == IoUringResult::Ok, "unable to prepare recvmsg");
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
              static_cast<uint16_t>(req->type()), fmt::ptr(req));
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
    case Request::RequestType::Send:
      ENVOY_LOG(trace, "receive send request completion, fd = {}, req = {}", req->socket().fd(),
                fmt::ptr(req));
      req->socket().onSend(req, result, injected);
      break;
    case Request::RequestType::Recv:
      ENVOY_LOG(trace, "receive recv request completion, fd = {}, req = {}", req->socket().fd(),
                fmt::ptr(req));
      req->socket().onRecv(req, result, injected);
      break;
    case Request::RequestType::SendMsg:
      ENVOY_LOG(trace, "receive sendmsg request completion, fd = {}, req = {}", req->socket().fd(),
                fmt::ptr(req));
      req->socket().onSendmsg(req, result, injected);
      break;
    case Request::RequestType::RecvMsg:
      ENVOY_LOG(trace, "receive recvmsg request completion, fd = {}, req = {}", req->socket().fd(),
                fmt::ptr(req));
      req->socket().onRecvmsg(req, result, injected);
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
  ENVOY_LOG(trace, "close the socket, fd = {}, status = {}", fd_, static_cast<int>(status_));

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

void IoUringServerSocket::onSend(Request* req, int32_t result, bool injected) {
  IoUringSocketEntry::onSend(req, result, injected);
  ENVOY_LOG(trace, "onSend with result {}, fd = {}, injected = {}", result, fd_, injected);

  if (!injected) {
    write_or_shutdown_req_ = nullptr;
  }

  // Handle send completion similar to write completion.
  if (result > 0) {
    // For send operations, we don't track a write buffer, so just callback.
    if (!shutdown_.has_value() && status_ != Closed) {
      onWriteCompleted(result);
    }
  } else {
    if (!shutdown_.has_value() && status_ != Closed) {
      status_ = RemoteClosed;
      if (result == -EPIPE) {
        IoUringSocketEntry::onRemoteClose();
      } else {
        onWriteCompleted(result);
      }
    }
  }
}

void IoUringServerSocket::onRecv(Request* req, int32_t result, bool injected) {
  IoUringSocketEntry::onRecv(req, result, injected);
  ENVOY_LOG(trace, "onRecv with result {}, fd = {}, injected = {}", result, fd_, injected);

  if (!injected) {
    read_req_ = nullptr;
    if (status_ == Closed && write_or_shutdown_req_ == nullptr && read_cancel_req_ == nullptr &&
        write_or_shutdown_cancel_req_ == nullptr) {
      if (result > 0 && keep_fd_open_) {
        // Move received data to read buffer.
        RecvRequest* recv_req = static_cast<RecvRequest*>(req);
        Buffer::BufferFragment* fragment = new Buffer::BufferFragmentImpl(
            recv_req->buf_.release(), result,
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

  // Move received data to read buffer similar to readv.
  if (result > 0) {
    RecvRequest* recv_req = static_cast<RecvRequest*>(req);
    Buffer::BufferFragment* fragment = new Buffer::BufferFragmentImpl(
        recv_req->buf_.release(), result,
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

  // Handle similar to onRead.
  if (status_ == Initialized || status_ == Closed) {
    return;
  }

  if (status_ == ReadEnabled) {
    if (read_buf_.length() > 0) {
      onReadCompleted(static_cast<int32_t>(read_buf_.length()));
    } else if (read_error_.has_value() && read_error_ < 0) {
      onReadCompleted(read_error_.value());
      read_error_.reset();
    }

    if (read_error_.has_value() && read_error_ == 0 && !enable_close_event_) {
      ENVOY_LOG(trace, "recv remote closed from socket, fd = {}", fd_);
      onReadCompleted(read_error_.value());
      read_error_.reset();
      return;
    }
  }

  // Continue with next recv if needed.
  if (status_ == ReadEnabled) {
    if (!read_error_.has_value() || read_error_.value() != 0) {
      submitReadRequest(); // This will use readv by default, but could be changed to recv.
    }
  } else if (status_ == ReadDisabled) {
    if (!read_error_.has_value()) {
      submitReadRequest();
    }
  }
}

void IoUringServerSocket::onSendmsg(Request* req, int32_t result, bool injected) {
  IoUringSocketEntry::onSendmsg(req, result, injected);
  ENVOY_LOG(trace, "onSendmsg with result {}, fd = {}, injected = {}", result, fd_, injected);

  // Handle similar to send.
  if (!injected) {
    write_or_shutdown_req_ = nullptr;
  }

  if (result > 0) {
    if (!shutdown_.has_value() && status_ != Closed) {
      onWriteCompleted(result);
    }
  } else {
    if (!shutdown_.has_value() && status_ != Closed) {
      status_ = RemoteClosed;
      if (result == -EPIPE) {
        IoUringSocketEntry::onRemoteClose();
      } else {
        onWriteCompleted(result);
      }
    }
  }
}

void IoUringServerSocket::onRecvmsg(Request* req, int32_t result, bool injected) {
  IoUringSocketEntry::onRecvmsg(req, result, injected);
  ENVOY_LOG(trace, "onRecvmsg with result {}, fd = {}, injected = {}", result, fd_, injected);

  if (!injected) {
    read_req_ = nullptr;
    if (status_ == Closed && write_or_shutdown_req_ == nullptr && read_cancel_req_ == nullptr &&
        write_or_shutdown_cancel_req_ == nullptr) {
      if (result > 0 && keep_fd_open_) {
        // Move received data to read buffer.
        RecvMsgRequest* recvmsg_req = static_cast<RecvMsgRequest*>(req);
        Buffer::BufferFragment* fragment = new Buffer::BufferFragmentImpl(
            recvmsg_req->buf_.release(), result,
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

  // Handle received data and metadata from recvmsg.
  if (result > 0) {
    RecvMsgRequest* recvmsg_req = static_cast<RecvMsgRequest*>(req);

    // Store received data.
    Buffer::BufferFragment* fragment = new Buffer::BufferFragmentImpl(
        recvmsg_req->buf_.release(), result,
        [](const void* data, size_t, const Buffer::BufferFragmentImpl* this_fragment) {
          delete[] reinterpret_cast<const uint8_t*>(data);
          delete this_fragment;
        });
    read_buf_.addBufferFragment(*fragment);

    // TODO: Handle control messages and peer address from recvmsg_req->msg_
    // This would be used for UDP sockets to get source address and control info
  } else {
    if (result != -ECANCELED) {
      read_error_ = result;
    }
  }

  // Handle similar to onRead/onRecv.
  if (status_ == Initialized || status_ == Closed) {
    return;
  }

  if (status_ == ReadEnabled) {
    if (read_buf_.length() > 0) {
      onReadCompleted(static_cast<int32_t>(read_buf_.length()));
    } else if (read_error_.has_value() && read_error_ < 0) {
      onReadCompleted(read_error_.value());
      read_error_.reset();
    }
  }

  // Continue with recvmsg if needed for connection-less sockets.
  if (status_ == ReadEnabled && (!read_error_.has_value() || read_error_.value() != 0)) {
    submitReadRequest(); // Could be changed to submit recvmsg for UDP.
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
            result, fd_, injected, static_cast<int>(status_), enable_close_event_);
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
            injected, static_cast<int>(status_));
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
    const IoUringMode mode = parent_.getMode();

    switch (mode) {
    case IoUringMode::SendRecv: {
      // Use recv operation for simple streaming data.
      ENVOY_LOG(trace, "submit recv request, buffer_size = {}, fd = {}, mode = SendRecv",
                parent_.getReadBufferSize(), fd_);
      read_req_ = parent_.submitRecvRequest(*this, nullptr, parent_.getReadBufferSize(), 0);
      break;
    }
    case IoUringMode::SendmsgRecvmsg: {
      // Use recvmsg operation for advanced scenarios.
      // Allocate space for control messages (for ancillary data).
      constexpr size_t control_size = 256; // Standard size for control messages.
      ENVOY_LOG(trace,
                "submit recvmsg request, buffer_size = {}, control_size = {}, fd = {}, mode = "
                "SendmsgRecvmsg",
                parent_.getReadBufferSize(), control_size, fd_);
      read_req_ = parent_.submitRecvmsgRequest(*this, nullptr, 0);
      break;
    }
    case IoUringMode::ReadWritev:
    default: {
      // Use readv operation (default/backward compatible behavior).
      ENVOY_LOG(trace, "submit read request, buffer_size = {}, fd = {}, mode = ReadWritev",
                parent_.getReadBufferSize(), fd_);
      read_req_ = parent_.submitReadRequest(*this);
      break;
    }
    }
  }
}

void IoUringServerSocket::submitWriteOrShutdownRequest() {
  if (!write_or_shutdown_req_) {
    if (write_buf_.length() > 0) {
      const IoUringMode mode = parent_.getMode();

      switch (mode) {
      case IoUringMode::SendRecv: {
        // Use send operation for simple streaming data.
        // Linearize buffer for optimal send performance.
        const void* data = write_buf_.linearize(write_buf_.length());
        ENVOY_LOG(trace, "submit send request, write_buf size = {}, fd = {}, mode = SendRecv",
                  write_buf_.length(), fd_);
        write_or_shutdown_req_ =
            parent_.submitSendRequest(*this, data, write_buf_.length(), MSG_NOSIGNAL);
        break;
      }
      case IoUringMode::SendmsgRecvmsg: {
        // Use sendmsg operation for scatter-gather I/O.
        Buffer::RawSliceVector slices = write_buf_.getRawSlices(IOV_MAX);
        ENVOY_LOG(trace,
                  "submit sendmsg request, write_buf size = {}, num_iovecs = {}, fd = {}, mode = "
                  "SendmsgRecvmsg",
                  write_buf_.length(), slices.size(), fd_);

        // Prepare msghdr structure for sendmsg.
        // Create temporary msghdr on stack - SendMsgRequest will deep copy it.
        struct msghdr msg;
        memset(&msg, 0, sizeof(msg));

        // Create temporary iovec array on stack.
        auto iov_array = std::make_unique<struct iovec[]>(slices.size());
        for (size_t i = 0; i < slices.size(); i++) {
          iov_array[i].iov_base = slices[i].mem_;
          iov_array[i].iov_len = slices[i].len_;
        }

        msg.msg_iov = iov_array.get();
        msg.msg_iovlen = slices.size();

        // SendMsgRequest constructor will deep copy the msg structure and iovec array.
        write_or_shutdown_req_ = parent_.submitSendmsgRequest(*this, &msg, MSG_NOSIGNAL);
        break;
      }
      case IoUringMode::ReadWritev:
      default: {
        // Use writev operation (default/backward compatible behavior).
        Buffer::RawSliceVector slices = write_buf_.getRawSlices(IOV_MAX);
        ENVOY_LOG(trace,
                  "submit write request, write_buf size = {}, num_iovecs = {}, fd = {}, mode = "
                  "ReadWritev",
                  write_buf_.length(), slices.size(), fd_);
        write_or_shutdown_req_ = parent_.submitWriteRequest(*this, slices);
        break;
      }
      }
    } else if (shutdown_.has_value() && !shutdown_.value()) {
      write_or_shutdown_req_ = parent_.submitShutdownRequest(*this, SHUT_WR);
    } else if (status_ == Closed && read_req_ == nullptr && read_cancel_req_ == nullptr &&
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
            injected, static_cast<int>(status_));

  read_req_ = nullptr;
  // Socket may be closed on connecting like binding error. In this situation we may not callback
  // on connecting completion.
  if (status_ == Closed) {
    if (write_or_shutdown_req_ == nullptr && read_cancel_req_ == nullptr &&
        write_or_shutdown_cancel_req_ == nullptr) {
      closeInternal();
    }
    return;
  }

  if (result == 0) {
    enableRead();
  }
  // Calls parent injectCompletion() directly since we want to send connect result back to the IO
  // handle.
  parent_.injectCompletion(*this, Request::RequestType::Write, result);
}

} // namespace Io
} // namespace Envoy
