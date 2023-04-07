#include "source/common/io/io_uring_worker_impl.h"

#include <sys/socket.h>

namespace Envoy {
namespace Io {

BaseRequest::BaseRequest(uint32_t type, IoUringSocket& socket) : type_(type), socket_(socket) {}

AcceptRequest::AcceptRequest(IoUringSocket& socket) : BaseRequest(RequestType::Accept, socket) {}

ReadRequest::ReadRequest(IoUringSocket& socket, uint32_t size)
    : BaseRequest(RequestType::Read, socket), buf_(std::make_unique<uint8_t[]>(size)),
      iov_(std::make_unique<struct iovec>()) {
  iov_->iov_base = buf_.get();
  iov_->iov_len = size;
}

WriteRequest::WriteRequest(IoUringSocket& socket, const Buffer::RawSliceVector& slices)
    : BaseRequest(RequestType::Write, socket),
      iov_(std::make_unique<struct iovec[]>(slices.size())) {
  for (size_t i = 0; i < slices.size(); i++) {
    iov_[i].iov_base = slices[i].mem_;
    iov_[i].iov_len = slices[i].len_;
  }
}

IoUringSocketEntry::IoUringSocketEntry(os_fd_t fd, IoUringWorkerImpl& parent,
                                       IoUringHandler& io_uring_handler)
    : fd_(fd), parent_(parent), io_uring_handler_(io_uring_handler) {}

void IoUringSocketEntry::cleanup() {
  parent_.removeInjectedCompletion(*this);
  IoUringSocketEntryPtr socket = parent_.removeSocket(*this);
  parent_.dispatcher().deferredDelete(std::move(socket));
}

void IoUringSocketEntry::injectCompletion(uint32_t type) {
  // Avoid injecting the same completion type multiple times.
  if (injected_completions_ & type) {
    ENVOY_LOG(trace,
              "ignore injected completion since there already has one, injected_completions_: {}, "
              "type: {}",
              injected_completions_, type);
    return;
  }
  injected_completions_ |= type;
  parent_.injectCompletion(*this, type, -EAGAIN);
}

IoUringWorkerImpl::IoUringWorkerImpl(uint32_t io_uring_size, bool use_submission_queue_polling,
                                     uint32_t accept_size, uint32_t read_buffer_size,
                                     uint32_t connect_timeout_ms, uint32_t write_timeout_ms,
                                     Event::Dispatcher& dispatcher)
    : IoUringWorkerImpl(std::make_unique<IoUringImpl>(io_uring_size, use_submission_queue_polling,
                                                      connect_timeout_ms, write_timeout_ms),
                        accept_size, read_buffer_size, dispatcher) {}

IoUringWorkerImpl::IoUringWorkerImpl(std::unique_ptr<IoUring> io_uring, uint32_t accept_size,
                                     uint32_t read_buffer_size, Event::Dispatcher& dispatcher)
    : io_uring_(std::move(io_uring)), accept_size_(accept_size),
      read_buffer_size_(read_buffer_size), dispatcher_(dispatcher) {
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
    if (socket->getStatus() != IoUringSocketStatus::CLOSING) {
      socket->close();
    }
  }

  while (!sockets_.empty()) {
    ENVOY_LOG(trace, "still left {} sockets are not closed", sockets_.size());
    for (auto& socket : sockets_) {
      ENVOY_LOG(trace, "the socket fd = {} not closed", socket->fd());
    }
    dispatcher_.run(Event::Dispatcher::RunType::NonBlock);
  }

  dispatcher_.clearDeferredDeleteList();
}

IoUringSocket& IoUringWorkerImpl::addAcceptSocket(os_fd_t fd, IoUringHandler& handler) {
  ENVOY_LOG(trace, "add accept socket, fd = {}", fd);
  std::unique_ptr<IoUringAcceptSocket> socket =
      std::make_unique<IoUringAcceptSocket>(fd, *this, handler, accept_size_);
  LinkedList::moveIntoListBack(std::move(socket), sockets_);
  return *sockets_.back();
}

IoUringSocket& IoUringWorkerImpl::addServerSocket(os_fd_t fd, IoUringHandler& handler) {
  ENVOY_LOG(trace, "add server socket, fd = {}", fd);
  std::unique_ptr<IoUringServerSocket> socket =
      std::make_unique<IoUringServerSocket>(fd, *this, handler);
  LinkedList::moveIntoListBack(std::move(socket), sockets_);
  return *sockets_.back();
}

IoUringSocket& IoUringWorkerImpl::addClientSocket(os_fd_t fd, IoUringHandler&) {
  ENVOY_LOG(trace, "add client socket, fd = {}", fd);
  PANIC("not implemented");
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
  Request* req = new BaseRequest(RequestType::Connect, socket);

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
  Request* req = new BaseRequest(RequestType::Close, socket);

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
  Request* req = new BaseRequest(RequestType::Cancel, socket);

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
  Request* req = new BaseRequest(RequestType::Shutdown, socket);

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
  return socket.removeFromList(sockets_);
}

void IoUringWorkerImpl::injectCompletion(IoUringSocket& socket, uint32_t type, int32_t result) {
  Request* req = new BaseRequest(type, socket);
  io_uring_->injectCompletion(socket.fd(), req, result);
  file_event_->activate(Event::FileReadyType::Read);
}

void IoUringWorkerImpl::removeInjectedCompletion(IoUringSocket& socket) {
  io_uring_->removeInjectedCompletion(socket.fd());
}

void IoUringWorkerImpl::onFileEvent() {
  ENVOY_LOG(trace, "io uring worker, on file event");
  delay_submit_ = true;
  io_uring_->forEveryCompletion([](void* user_data, int32_t result, bool injected) {
    if (user_data == nullptr) {
      return;
    }
    auto req = static_cast<Request*>(user_data);

    switch (req->type()) {
    case RequestType::Accept:
      ENVOY_LOG(trace, "receive accept request completion, fd = {}, req = {}", req->socket().fd(),
                fmt::ptr(req));
      req->socket().onAccept(req, result, injected);
      break;
    case RequestType::Connect:
      ENVOY_LOG(trace, "receive connect request completion, fd = {}, req = {}", req->socket().fd(),
                fmt::ptr(req));
      req->socket().onConnect(result, injected);
      break;
    case RequestType::Read:
      ENVOY_LOG(trace, "receive Read request completion, fd = {}, req = {}", req->socket().fd(),
                fmt::ptr(req));
      req->socket().onRead(req, result, injected);
      break;
    case RequestType::Write:
      ENVOY_LOG(trace, "receive write request completion, fd = {}, req = {}", req->socket().fd(),
                fmt::ptr(req));
      req->socket().onWrite(result, injected);
      break;
    case RequestType::Close:
      ENVOY_LOG(trace, "receive close request completion, fd = {}, req = {}", req->socket().fd(),
                fmt::ptr(req));
      req->socket().onClose(result, injected);
      break;
    case RequestType::Cancel:
      ENVOY_LOG(trace, "receive cancel request completion, fd = {}, req = {}", req->socket().fd(),
                fmt::ptr(req));
      req->socket().onCancel(result, injected);
      break;
    case RequestType::Shutdown:
      ENVOY_LOG(trace, "receive shutdown request completion, fd = {}, req = {}", req->socket().fd(),
                fmt::ptr(req));
      req->socket().onShutdown(result, injected);
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
                                         IoUringHandler& io_uring_handler, uint32_t accept_size)
    : IoUringSocketEntry(fd, parent, io_uring_handler), accept_size_(accept_size),
      requests_(std::vector<Request*>(accept_size_, nullptr)) {
  enable();
}

void IoUringAcceptSocket::close() {
  IoUringSocketEntry::close();

  if (!request_count_) {
    parent_.submitCloseRequest(*this);
    return;
  }

  // TODO (soulxu): after kernel 5.19, we are able to cancel all requests for the specific fd.
  // TODO(zhxie): there may be races between main thread and worker thread in request_count_ and
  // requests_ in close(). When there is a listener draining, all server sockets in the listener
  // will be closed by the main thread. Though there may be races, it is still safe to
  // submitCancelRequest since io_uring can accept cancelling an invalid user_data.
  for (auto req : requests_) {
    if (req != nullptr) {
      parent_.submitCancelRequest(*this, req);
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
  for (auto req : requests_) {
    if (req != nullptr) {
      parent_.submitCancelRequest(*this, req);
    }
  }
}

void IoUringAcceptSocket::onClose(int32_t result, bool injected) {
  IoUringSocketEntry::onClose(result, injected);
  ASSERT(!injected);
  cleanup();
}

void IoUringAcceptSocket::onAccept(Request* req, int32_t result, bool injected) {
  IoUringSocketEntry::onAccept(req, result, injected);
  ASSERT(!injected);
  // If there is no pending accept request and the socket is going to close, submit close request.
  AcceptRequest* accept_req = static_cast<AcceptRequest*>(req);
  requests_[accept_req->i_] = nullptr;
  request_count_--;
  if (status_ == CLOSING) {
    if (!request_count_) {
      parent_.submitCloseRequest(*this);
    }
  }

  // If the socket is not enabled, drop all following actions to all accepted fds.
  if (status_ == ENABLED) {
    // Submit a new accept request for the next connection.
    submitRequests();
    if (result != -ECANCELED) {
      ENVOY_LOG(trace, "accept new socket, fd = {}, result = {}", fd_, result);
      AcceptedSocketParam param{result, &accept_req->remote_addr_, accept_req->remote_addr_len_};
      io_uring_handler_.onAcceptSocket(param);
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
                                         IoUringHandler& io_uring_handler)
    : IoUringSocketEntry(fd, parent, io_uring_handler) {
  enable();
}

void IoUringServerSocket::close() {
  ENVOY_LOG(trace, "close the socket, fd = {}, status = {}", fd_, status_);

  if (status_ == SHUTDOWN_WRITE || status_ == CLOSE_AFTER_SHUTDOWN_WRITE) {
    ENVOY_LOG(trace, "wait for the shutdown write done before close");
    status_ = CLOSE_AFTER_SHUTDOWN_WRITE;
    return;
  }

  IoUringSocketEntry::close();

  if (read_req_ == nullptr && write_req_ == nullptr && cancel_req_ == nullptr) {
    ENVOY_LOG(trace, "ready to close, fd = {}", fd_);
    close_req_ = parent_.submitCloseRequest(*this);
    return;
  }

  if (cancel_req_ == nullptr && read_req_ != nullptr) {
    ENVOY_LOG(trace, "cancel the read request, fd = {}", fd_);
    cancel_req_ = parent_.submitCancelRequest(*this, read_req_);
  }
}

void IoUringServerSocket::enable() {
  IoUringSocketEntry::enable();
  ENVOY_LOG(trace, "enable, fd = {}", fd_);

  // Continue processing read buffer remained by the previous read.
  if (buf_.length() > 0 || read_error_.has_value()) {
    ENVOY_LOG(trace, "continue reading from socket, fd = {}, size = {}", fd_, buf_.length());
    injectCompletion(RequestType::Read);
    return;
  }

  submitReadRequest();
}

void IoUringServerSocket::disable() {
  // We didn't implement the disable socket after shutdown.
  ASSERT(status_ != SHUTDOWN_WRITE && status_ != CLOSE_AFTER_SHUTDOWN_WRITE &&
         status_ != ALREADY_SHUTDOWN);
  IoUringSocketEntry::disable();
}

void IoUringServerSocket::write(Buffer::Instance& data) {
  ENVOY_LOG(trace, "write, buffer size = {}, fd = {}", data.length(), fd_);

  write_buf_.move(data);

  if (write_req_ != nullptr) {
    ENVOY_LOG(trace, "write already submited, fd = {}", fd_);
    return;
  }

  ENVOY_LOG(trace, "write buf, size = {}, slices = {}", write_buf_.length(),
            write_buf_.getRawSlices().size());
  submitWriteRequest();
}

uint64_t IoUringServerSocket::write(const Buffer::RawSlice* slices, uint64_t num_slice) {
  ENVOY_LOG(trace, "write, num_slices = {}, fd = {}", num_slice, fd_);

  uint64_t bytes_written = 0;
  for (uint64_t i = 0; i < num_slice; i++) {
    write_buf_.add(slices[i].mem_, slices[i].len_);
    bytes_written += slices[i].len_;
  }

  if (write_req_ != nullptr) {
    ENVOY_LOG(trace, "write already submited, fd = {}", fd_);
    return bytes_written;
  }

  submitWriteRequest();
  return bytes_written;
}

void IoUringServerSocket::shutdown(int how) {
  ENVOY_LOG(trace, "shutdown the socket, fd = {}, how = {}", fd_, how);
  if (how != SHUT_WR) {
    PANIC("only the SHUT_WR implemented");
  }

  status_ = IoUringSocketStatus::SHUTDOWN_WRITE;
  if (write_req_ != nullptr) {
    return;
  }

  parent_.submitShutdownRequest(*this, how);
}

void IoUringServerSocket::onClose(int32_t result, bool injected) {
  IoUringSocketEntry::onClose(result, injected);
  ASSERT(!injected);
  cleanup();
}

// TODO(zhxie): concern submit multiple read requests or submit read request in advance to improve
// performance in the next iteration.
void IoUringServerSocket::onRead(Request* req, int32_t result, bool injected) {
  IoUringSocketEntry::onRead(req, result, injected);

  ENVOY_LOG(trace, "onRead with result {}, fd = {}, injected = {}, status_ = {}", result, fd_,
            injected, status_);
  if (!injected) {
    read_req_ = nullptr;
    // Close if it is in closing status and no write request.
    if (status_ == CLOSING && close_req_ == nullptr && write_req_ == nullptr &&
        cancel_req_ == nullptr) {
      ENVOY_LOG(trace, "ready to close, fd = {}", fd_);
      close_req_ = parent_.submitCloseRequest(*this);
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
    buf_.addBufferFragment(*fragment);
  } else {
    if (result != -ECANCELED) {
      read_error_ = result;
    }
  }

  // If the socket is enabled, notify handler to read.
  if (status_ == ENABLED || status_ == DISABLED || status_ == SHUTDOWN_WRITE ||
      status_ == CLOSE_AFTER_SHUTDOWN_WRITE || status_ == ALREADY_SHUTDOWN) {
    if (buf_.length() > 0 && status_ != DISABLED) {
      ENVOY_LOG(trace, "read from socket, fd = {}, result = {}", fd_, buf_.length());
      ReadParam param{buf_, static_cast<int32_t>(buf_.length())};
      io_uring_handler_.onRead(param);
      ENVOY_LOG(trace, "after read from socket, fd = {}, remain = {}", fd_, buf_.length());
    } else if (read_error_.has_value() && read_error_ <= 0) {
      // When the socket is disabled, the close event still need to be monitored and delivered.
      if (status_ != DISABLED) {
        ENVOY_LOG(trace, "read error from socket, fd = {}, result = {}", fd_, read_error_.value());
        ReadParam param{buf_, read_error_.value()};
        io_uring_handler_.onRead(param);
        read_error_.reset();
      } else if (status_ == DISABLED && read_error_.has_value() && read_error_ == 0) {
        ENVOY_LOG(trace,
                  "read disabled and got a remote close from socket, raise the close event, fd = "
                  "{}, result = {}",
                  fd_, read_error_.value());
        io_uring_handler_.onClose();
        read_error_.reset();
        return;
      }
    }
    // The socket may be disabled during handler onRead callback, check it again here.
    if (status_ == ENABLED || status_ == SHUTDOWN_WRITE || status_ == CLOSE_AFTER_SHUTDOWN_WRITE ||
        status_ == ALREADY_SHUTDOWN) {
      // If the read error is zero, it means remote close, then needn't new request.
      if (!read_error_.has_value() || read_error_.value() != 0) {
        // Submit a read accept request for the next read.
        submitReadRequest();
      }
    }
  }
}

void IoUringServerSocket::onWrite(int32_t result, bool injected) {
  IoUringSocketEntry::onWrite(result, injected);

  ENVOY_LOG(trace, "onWrite with result {}, fd = {}, injected = {}", result, fd_, injected);

  // Only cleanup request for non-injected write request.
  if (!injected) {
    write_req_ = nullptr;
  }

  if (status_ == CLOSING) {
    // Close if it is in closing status and no read request.
    if (read_req_ == nullptr && close_req_ == nullptr && cancel_req_ == nullptr) {
      close_req_ = parent_.submitCloseRequest(*this);
    }
    return;
  }

  // If this is injected request, then notify the handler directly.
  if (injected) {
    ENVOY_LOG(trace,
              "there is a inject event, and same time we have regular write request, fd = {}", fd_);
    WriteParam param{result};
    io_uring_handler_.onWrite(param);
    return;
  }

  // If result is EAGAIN, the request should be submitted again.
  if (result <= 0 && result != -EAGAIN) {
    WriteParam param{result};
    io_uring_handler_.onWrite(param);
    if (status_ == SHUTDOWN_WRITE || status_ == CLOSE_AFTER_SHUTDOWN_WRITE) {
      parent_.submitShutdownRequest(*this, SHUT_WR);
    }
    return;
  }

  if (result > 0) {
    write_buf_.drain(result);
    ENVOY_LOG(trace, "drain write buf, drain size = {}, fd = {}", result, fd_);
  }

  // If there are data in the buffer, then we need to flush them before shutdown
  // the socket.
  if (write_buf_.length() > 0) {
    ENVOY_LOG(trace, "continue write buf since still have data, size = {}, fd = {}",
              write_buf_.length(), fd_);
    submitWriteRequest();
  } else {
    if (status_ == SHUTDOWN_WRITE || status_ == CLOSE_AFTER_SHUTDOWN_WRITE) {
      parent_.submitShutdownRequest(*this, SHUT_WR);
    }
  }
}

void IoUringServerSocket::onCancel(int32_t result, bool injected) {
  IoUringSocketEntry::onCancel(result, injected);
  ASSERT(!injected);
  ENVOY_LOG(trace, "cancel done, result = {}, fd = {}", result, fd_);

  cancel_req_ = nullptr;
  if (status_ == CLOSING && read_req_ == nullptr && write_req_ == nullptr) {
    ENVOY_LOG(trace, "ready to close, fd = {}", fd_);
    close_req_ = parent_.submitCloseRequest(*this);
  }
}

void IoUringServerSocket::onShutdown(int32_t result, bool injected) {
  IoUringSocketEntry::onCancel(result, injected);
  ASSERT(!injected);
  ENVOY_LOG(trace, "shutdown done, result = {}, fd = {}", result, fd_);
  if (status_ == CLOSE_AFTER_SHUTDOWN_WRITE) {
    ENVOY_LOG(trace, "close after shutdown write, fd = {}", fd_);
    status_ = ALREADY_SHUTDOWN;
    close();
    return;
  }
  status_ = ALREADY_SHUTDOWN;
}

void IoUringServerSocket::submitReadRequest() {
  if (!read_req_) {
    read_req_ = parent_.submitReadRequest(*this);
  }
}

void IoUringServerSocket::submitWriteRequest() {
  ASSERT(write_req_ == nullptr);
  ASSERT(iovecs_ == nullptr);

  Buffer::RawSliceVector slices = write_buf_.getRawSlices(IOV_MAX);

  ENVOY_LOG(trace, "submit write request, write_buf size = {}, num_iovecs = {}, fd = {}",
            write_buf_.length(), slices.size(), fd_);

  write_req_ = parent_.submitWriteRequest(*this, slices);
}

} // namespace Io
} // namespace Envoy
