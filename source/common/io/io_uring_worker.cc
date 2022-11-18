#include "source/common/io/io_uring_worker.h"
#include "io_uring.h"
#include "io_uring_impl.h"
#include <sys/socket.h>

namespace Envoy {
namespace Io {

void IoUringAcceptSocket::onAccept(int32_t result) {
  accept_req_ = nullptr;
  if (result < 0) {
    ENVOY_LOG(debug, "Accept request failed");
    
    if (is_closing_ && result == -ECANCELED && cancel_req_ == nullptr) {
      close_req_ = parent_.submitCloseRequest(*this);
    }
    return;
  }

  if (is_disabled_) {
    ENVOY_LOG(debug, "accept new socket but disabled, connect fd = {}", result);
    is_pending_accept_ = true;
    connection_fd_ = result;
    return;
  }

  connection_fd_ = result;

  ENVOY_LOG(debug, "New socket accepted, address = {}, connect fd = {}",
    Network::Address::addressFromSockAddrOrThrow(remote_addr_, remote_addr_len_, false)->asString(), connection_fd_);

  AcceptedSocketParam param{connection_fd_, &remote_addr_, remote_addr_len_};
  io_uring_handler_.onAcceptSocket(param);
  accept_req_ = parent_.submitAcceptRequest(*this, &remote_addr_, &remote_addr_len_);
}

void IoUringAcceptSocket::onCancel(int32_t) {
  cancel_req_ = nullptr;
  accept_req_ = nullptr;
  ENVOY_LOG(debug, "cancel request done, pending_accept = {}", is_pending_accept_);
  if (is_closing_) {
    if (is_pending_accept_) {
      // close the pending socket
      ENVOY_LOG(trace, "close the pending fd, fd = {}", connection_fd_);
      ::close(connection_fd_);
      is_pending_accept_ = false;
      close_req_ = parent_.submitCloseRequest(*this);
    } else {
      close_req_ = parent_.submitCloseRequest(*this);
    }
  }
}

void IoUringAcceptSocket::onClose(int32_t) {
  close_req_ = nullptr;
  ENVOY_LOG(debug, "close request done, pending_accept = {}", is_pending_accept_);
  if (is_pending_accept_) {
    is_pending_accept_ = false;
    // Close the accepted socket directly when there is pending one.
    ::close(connection_fd_);
  }
  ENVOY_LOG(debug, "the socket {} is ready to end", fd_);
  std::unique_ptr<IoUringSocket> self = parent_.removeSocket(fd_);
  parent_.dispatcher().deferredDelete(std::move(self));
}

void IoUringAcceptSocket::start() {
  accept_req_ = parent_.submitAcceptRequest(*this, &remote_addr_, &remote_addr_len_);
}

void IoUringAcceptSocket::close() {
  ENVOY_LOG(trace, "close socket fd = {}, accept_req_ = {}, cancel_req_ = {}", fd_, accept_req_ == nullptr, cancel_req_ == nullptr);

  if (accept_req_ != nullptr) {
    is_closing_ = true;
    if (cancel_req_ == nullptr) {
      ENVOY_LOG(debug, "submit cancel request for the accept since close, fd = {}, cancel_req = {}", fd_, cancel_req_ == nullptr);
      cancel_req_ = parent_.submitCancelRequest(*this, accept_req_);
    }
    return;
  }

  ENVOY_LOG(debug, "submit close request for the accept");
  close_req_ = parent_.submitCloseRequest(*this);
  is_disabled_ = true;
}

void IoUringAcceptSocket::enable() {
  ENVOY_LOG(debug, "enable accept socket, fd = {}", fd_);
  is_disabled_ = false;
  if (is_pending_accept_) {
    is_pending_accept_ = false;
    onAccept(0);
  }
}

void IoUringAcceptSocket::disable() {
  if (accept_req_ != nullptr) {
    if (cancel_req_ == nullptr) {
      ENVOY_LOG(debug, "submit cancel request for the accept since disable, fd = {}", fd_);
      cancel_req_ = parent_.submitCancelRequest(*this, accept_req_);
    }
  }
  is_disabled_ = true;
}

void IoUringWorkerImpl::onFileEvent() {
  ENVOY_LOG(trace, "io uring worker, on file event");
  io_uring_impl_.forEveryCompletion([](void* user_data, int32_t result) {
    auto req = static_cast<Io::Request*>(user_data);

    ENVOY_LOG(debug, "receive request completion, result = {}, req = {}", result, fmt::ptr(req));

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
        ENVOY_LOG(trace, "receive Read request completion, fd = {}, read_req = {}", req->io_uring_socket_->get().fd(), fmt::ptr(req));
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
  ENVOY_LOG(trace, "add accept socket, fd = {}", fd);
  std::unique_ptr<IoUringAcceptSocket> socket = std::make_unique<IoUringAcceptSocket>(fd, handler, *this);
  socket->start();
  sockets_.insert({fd, std::move(socket)});
  io_uring_impl_.submit();
}

void IoUringWorkerImpl::addServerSocket(os_fd_t fd, IoUringHandler& handler, uint32_t read_buffer_size) {
  ENVOY_LOG(trace, "add server socket, fd = {}", fd);
  std::unique_ptr<IoUringServerSocket> socket = std::make_unique<IoUringServerSocket>(fd, handler, *this, read_buffer_size);
  socket->start();
  sockets_.insert({fd, std::move(socket)});
  io_uring_impl_.submit();
}

void IoUringWorkerImpl::closeSocket(os_fd_t fd) {
  ENVOY_LOG(debug, "close socket, fd = {}", fd);
  auto socket_iter = sockets_.find(fd);
  ASSERT(socket_iter != sockets_.end());
  socket_iter->second->close();
}

std::unique_ptr<IoUringSocket> IoUringWorkerImpl::removeSocket(os_fd_t fd) {
  ENVOY_LOG(debug, "remove socket, fd = {}", fd);
  auto socket_iter = sockets_.find(fd);
  ASSERT(socket_iter != sockets_.end());
  auto socket = std::move(socket_iter->second);
  io_uring_impl_.removeInjectedCompletion(socket->fd());
  sockets_.erase(socket_iter);
  return socket;
}

Event::Dispatcher& IoUringWorkerImpl::dispatcher() {
  return dispatcher_;
}

IoUringSocket& IoUringWorkerImpl::getIoUringSocket(os_fd_t fd) {
  ENVOY_LOG(debug, "get socket, fd = {}", fd);
  auto socket_iter = sockets_.find(fd);
  ASSERT(socket_iter != sockets_.end());
  return *socket_iter->second;
}

Request* IoUringWorkerImpl::submitAcceptRequest(IoUringSocket& socket, sockaddr_storage* remote_addr,
                                         socklen_t* remote_addr_len) {
  ENVOY_LOG(trace, "submit accept request, fd = {}", socket.fd());
  Request* req = new Request();
  req->type_ = RequestType::Accept;
  req->io_uring_socket_ = socket;
  *remote_addr_len = sizeof(sockaddr_storage);
  auto res = io_uring_impl_.prepareAccept(socket.fd(), reinterpret_cast<struct sockaddr*>(remote_addr), remote_addr_len, req);
  if (res == Io::IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    io_uring_impl_.submit();
    res = io_uring_impl_.prepareAccept(socket.fd(), reinterpret_cast<struct sockaddr*>(remote_addr), remote_addr_len, req);
    RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare accept");
  }
  ENVOY_LOG(debug, "Submit new accept request");
  io_uring_impl_.submit();
  return req;
}

Request* IoUringWorkerImpl::submitCancelRequest(IoUringSocket& socket, Request* request_to_cancel) {
  Request* req = new Request();
  req->io_uring_socket_ = socket;
  req->type_ = RequestType::Cancel;

  ENVOY_LOG(trace, "submit cancel request, fd = {}, cancel req = {}", socket.fd(), fmt::ptr(req));

  auto res = io_uring_impl_.prepareCancel(request_to_cancel, req);
  if (res == Io::IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    io_uring_impl_.submit();
    res = io_uring_impl_.prepareCancel(request_to_cancel, req);
    RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare cancel");
  }
  io_uring_impl_.submit();
  return req;
}

Request* IoUringWorkerImpl::submitCloseRequest(IoUringSocket& socket) {
  Request* req = new Request();
  req->io_uring_socket_ = socket;
  req->type_ = RequestType::Close;

  ENVOY_LOG(trace, "submit close request, fd = {}, close req = {}", socket.fd(), fmt::ptr(req));

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
  Request* req = new Request();
  req->io_uring_socket_ = socket;
  req->type_ = RequestType::Read;

  ENVOY_LOG(trace, "submit read request, fd = {}, read req = {}", socket.fd(), fmt::ptr(req));

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

Request* IoUringWorkerImpl::submitWritevRequest(IoUringSocket& socket, struct iovec* iovecs, uint64_t num_vecs) {
  ENVOY_LOG(trace, "submit write request, fd = {}", socket.fd());
  Request* req = new Request();
  req->io_uring_socket_ = socket;
  req->type_ = RequestType::Write;
  
  auto res = io_uring_impl_.prepareWritev(socket.fd(), iovecs, num_vecs, 0, req);
  if (res == Io::IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    io_uring_impl_.submit();
    res = io_uring_impl_.prepareWritev(socket.fd(), iovecs, num_vecs, 0, req);
    RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare writev");
  }
  io_uring_impl_.submit();
  return req;
}

void IoUringWorkerImpl::injectCompletion(IoUringSocket& socket, RequestType type, int32_t result) {
  Request* req = new Request();
  req->io_uring_socket_ = socket;
  req->type_ = type;
  io_uring_impl_.injectCompletion(socket.fd(), req, result);
  file_event_->activate(Event::FileReadyType::Read);
}

void IoUringWorkerImpl::injectCompletion(os_fd_t fd, RequestType type, int32_t result) {
  auto socket_iter = sockets_.find(fd);
  ASSERT(socket_iter != sockets_.end());
  injectCompletion(*(socket_iter->second), type, result);
}

} // namespace Io
} // namespace Envoy