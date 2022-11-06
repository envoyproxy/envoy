#include "source/common/io/io_uring_worker.h"
#include "io_uring.h"
#include "io_uring_impl.h"

namespace Envoy {
namespace Io {

void IoUringAcceptSocket::onAccept(int32_t result) {
  if (result < 0) {
    ENVOY_LOG(debug, "Accept request failed");

    accept_req_ = nullptr;
    if (is_closing_ && result == -ECANCELED && cancel_req_ == nullptr) {
      close_req_ = parent_.submitCloseRequest(*this);
    }
    return;
  }

  if (is_disabled_) {
    ENVOY_LOG(debug, "accept new socket but disabled");
    is_pending_accept_ = true;
    return;
  }

  ENVOY_LOG(debug, "New socket accepted");
  connection_fd_ = result;
  AcceptedSocketParam param{connection_fd_, remote_addr_, remote_addr_len_};
  io_uring_handler_.onAcceptSocket(param);
  accept_req_ = nullptr;
  accept_req_ = parent_.submitAcceptRequest(*this, &remote_addr_, &remote_addr_len_);
}

void IoUringAcceptSocket::onCancel(int32_t) {
  cancel_req_ = nullptr;
  ENVOY_LOG(debug, "cancel request done");
  if (is_closing_ && accept_req_ == nullptr) {
    close_req_ = parent_.submitCloseRequest(*this);
  }
}

void IoUringAcceptSocket::onClose(int32_t) {
  close_req_ = nullptr;
  ENVOY_LOG(debug, "close request done");
  if (is_pending_accept_) {
    is_pending_accept_ = false;
    // Close the accepted socket directly when there is pending one.
    ::close(connection_fd_);
  }
  ENVOY_LOG(debug, "the socket {} is ready to end");
  std::unique_ptr<IoUringSocket> self = parent_.removeSocket(fd_);
  parent_.dispatcher().deferredDelete(std::move(self));
}

void IoUringAcceptSocket::start() {
  accept_req_ = parent_.submitAcceptRequest(*this, &remote_addr_, &remote_addr_len_);
}

void IoUringAcceptSocket::close() {
  if (accept_req_ != nullptr) {
    ENVOY_LOG(debug, "submit cancel request for the accept");
    is_closing_ = true;
    cancel_req_ = parent_.submitCancelRequest(*this, accept_req_);
    return;
  }

  ENVOY_LOG(debug, "submit close request for the accept");
  close_req_ = parent_.submitCloseRequest(*this);
  is_disabled_ = true;
}

void IoUringAcceptSocket::enable() {
  is_disabled_ = false;
  if (is_pending_accept_) {
    is_pending_accept_ = false;
    onAccept(0);
  }
}

void IoUringAcceptSocket::disable() {
  if (accept_req_ != nullptr) {
    ENVOY_LOG(debug, "submit cancel request for the accept");
    cancel_req_ = parent_.submitCancelRequest(*this, accept_req_);
  }
  is_disabled_ = true;
}

void IoUringWorkerImpl::onFileEvent() {
  io_uring_impl_.forEveryCompletion([](void* user_data, int32_t result) {
    auto req = static_cast<Io::Request*>(user_data);

    if (result < 0) {
      ENVOY_LOG(debug, "async request failed: {}", errorDetails(-result));
    }

    // temp log and temp fix for the old path, remove then when I fix the thing.
    switch(req->type_) {
      case RequestType::Accept:
        ENVOY_LOG(debug, "receive accept request completion");
        break;
      case RequestType::Connect:
        ENVOY_LOG(debug, "receive connect request completion");
        break;
      case RequestType::Read:
        ENVOY_LOG(debug, "receive Read request completion");
        break;
      case RequestType::Write:
        ENVOY_LOG(debug, "receive write request completion");
        break;
      case RequestType::Close:
        ENVOY_LOG(debug, "receive close request completion");
        break;
      case RequestType::Cancel:
        ENVOY_LOG(debug, "receive cancel request completion");
        break;
      case RequestType::Unknown:
        ENVOY_LOG(debug, "receive unknown request completion");
        break;
    }
  
    // temp fix for the old path
    if (!req->io_uring_socket_.has_value() && (req->type_ == RequestType::Close || req->type_ == RequestType::Cancel || result == -ECANCELED)) {
      return;
    }

    if (req->io_uring_socket_.has_value()) {
      switch(req->type_) {
      case RequestType::Accept:
        ENVOY_LOG(trace, "receive accept request completion, fd = {}", req->io_uring_socket_->get().fd());
        req->io_uring_socket_->get().onAccept(result);
        break;
      case RequestType::Connect:
        ENVOY_LOG(trace, "receive connect request completion, fd = {}", req->io_uring_socket_->get().fd());
        req->io_uring_socket_->get().onConnect(result);
        break;
      case RequestType::Read:
        ENVOY_LOG(trace, "receive Read request completion, fd = {}", req->io_uring_socket_->get().fd());
        req->io_uring_socket_->get().onRead(result);
        break;
      case RequestType::Write:
        ENVOY_LOG(trace, "receive write request completion, fd = {}", req->io_uring_socket_->get().fd());
        req->io_uring_socket_->get().onWrite(result);
        break;
      case RequestType::Close:
        ENVOY_LOG(trace, "receive close request completion, fd = {}", req->io_uring_socket_->get().fd());
        req->io_uring_socket_->get().onClose(result);
        break;
      case RequestType::Cancel:
        ENVOY_LOG(trace, "receive cancel request completion, fd = {}", req->io_uring_socket_->get().fd());
        req->io_uring_socket_->get().onCancel(result);
        break;
      case RequestType::Unknown:
        ENVOY_LOG(trace, "receive unknown request completion, fd = {}", req->io_uring_socket_->get().fd());
        break;
      }
    // For close, there is no iohandle value, but need to fix
    } else if (req->io_uring_handler_.has_value()) {
      req->io_uring_handler_->get().onRequestCompletion(*req, result);
    } else {
      ENVOY_LOG(debug, "no iohandle");
    }

    delete req;
  });
  io_uring_impl_.submit();
}

IoUringWorkerImpl::IoUringWorkerImpl(uint32_t io_uring_size, bool use_submission_queue_polling, Event::Dispatcher& dispatcher) :
    io_uring_impl_(io_uring_size, use_submission_queue_polling), dispatcher_(dispatcher) { }

void IoUringWorkerImpl::start() {
  // This means already registered the file event.
  if (file_event_ != nullptr) {
    return;
  }

  const os_fd_t event_fd = io_uring_impl_.registerEventfd();
  // We only care about the read event of Eventfd, since we only receive the
  // event here.
  file_event_ = dispatcher_.createFileEvent(
      event_fd, [this](uint32_t) { onFileEvent(); }, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
}

IoUring& IoUringWorkerImpl::get() {
  return io_uring_impl_;
}

void IoUringWorkerImpl::addAcceptSocket(os_fd_t fd, IoUringHandler& handler) {
  std::unique_ptr<IoUringAcceptSocket> socket = std::make_unique<IoUringAcceptSocket>(fd, handler, *this);
  socket->start();
  sockets_.insert({fd, std::move(socket)});
  io_uring_impl_.submit();
}

void IoUringWorkerImpl::closeSocket(os_fd_t fd) {
  auto socket_iter = sockets_.find(fd);
  ASSERT(socket_iter != sockets_.end());
  socket_iter->second->close();
}

std::unique_ptr<IoUringSocket> IoUringWorkerImpl::removeSocket(os_fd_t fd) {
  auto socket_iter = sockets_.find(fd);
  ASSERT(socket_iter != sockets_.end());
  auto socket = std::move(socket_iter->second);
  sockets_.erase(socket_iter);
  return socket;
}

Event::Dispatcher& IoUringWorkerImpl::dispatcher() {
  return dispatcher_;
}

Request* IoUringWorkerImpl::submitAcceptRequest(IoUringSocket& socket, struct sockaddr* remote_addr,
                                         socklen_t* remote_addr_len) {
  ENVOY_LOG(trace, "submit accept request, fd = {}", socket.fd());
  Request* req = new Request();
  req->type_ = RequestType::Accept;
  req->io_uring_socket_ = socket;
  auto res = io_uring_impl_.prepareAccept(socket.fd(), remote_addr, remote_addr_len, req);
  if (res == Io::IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    io_uring_impl_.submit();
    res = io_uring_impl_.prepareAccept(socket.fd(), remote_addr, remote_addr_len, req);
    RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare accept");
  }
  ENVOY_LOG(debug, "Submit new accept request");
  io_uring_impl_.submit();
  return req;
}

Request* IoUringWorkerImpl::submitCancelRequest(IoUringSocket& socket, Request* request_to_cancel) {
  ENVOY_LOG(trace, "submit cancel request, fd = {}", socket.fd());
  Request* req = new Request();
  req->io_uring_socket_ = socket;
  req->type_ = RequestType::Cancel;
  auto res = io_uring_impl_.prepareCancel(request_to_cancel, req);
  if (res == Io::IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    io_uring_impl_.submit();
    res = io_uring_impl_.prepareCancel(request_to_cancel, req);
    RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare cancel");
  }
  return req;
}

Request* IoUringWorkerImpl::submitCloseRequest(IoUringSocket& socket) {
  ENVOY_LOG(trace, "submit close request, fd = {}", socket.fd());
  Request* req = new Request();
  req->io_uring_socket_ = socket;
  req->type_ = RequestType::Close;
  auto res = io_uring_impl_.prepareClose(socket.fd(), req);
  if (res == Io::IoUringResult::Failed) {
     // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    io_uring_impl_.submit();
    res = io_uring_impl_.prepareClose(socket.fd(), req);
    RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare close");
  }
  io_uring_impl_.submit();
  return req;
}

Request* IoUringWorkerImpl::submitReadRequest(IoUringSocket& socket, struct iovec* iov) {
  ENVOY_LOG(trace, "submit read request, fd = {}", socket.fd());
  Request* req = new Request();
  req->io_uring_socket_ = socket;
  req->type_ = RequestType::Read;

  auto res = io_uring_impl_.prepareReadv(socket.fd(), iov, 1, 0, req);
  if (res == Io::IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    io_uring_impl_.submit();
    res = io_uring_impl_.prepareReadv(socket.fd(), iov, 1, 0, req);
    RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare readv");
  }
  io_uring_impl_.submit();
  return req;
}

void IoUringWorkerImpl::injectCompletion(IoUringSocket& socket, RequestType type, int32_t result) {
  Request* req = new Request();
  req->io_uring_socket_ = socket;
  req->type_ = type;
  io_uring_impl_.injectCompletion(req, result);
}

} // namespace Io
} // namespace Envoy