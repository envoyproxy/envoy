#include "source/common/io/io_uring_worker.h"
#include "io_uring.h"

namespace Envoy {
namespace Io {

void IoUringAcceptSocket::onRequestCompeltion(const Request& req, int32_t result) {
  if (req.type_ == RequestType::Accept) {
    if (result < 0) {
      ENVOY_LOG(debug, "Accept request failed");
      return;
    }
  
    ENVOY_LOG(debug, "New socket accepted");
    connection_fd_ = result;
    AcceptedSocketParam param{connection_fd_, remote_addr_, remote_addr_len_};
    io_uring_handler_.onAcceptSocket(param);
    accept_req_ = nullptr;
    submitRequest();
  } else if (req.type_ == RequestType::Cancel) {
    cancel_req_ = nullptr;
    ENVOY_LOG(debug, "cancel request done");
    if (close_req_ == nullptr) {
      ENVOY_LOG(debug, "the socket {} is ready to end");
      std::unique_ptr<IoUringSocket> self = parent_.removeSocket(fd_);
    }
  } else if (req.type_ == RequestType::Close) {
    close_req_ = nullptr;
    ENVOY_LOG(debug, "close request done");
    if (cancel_req_ == nullptr) {
      ENVOY_LOG(debug, "the socket {} is ready to end");
      std::unique_ptr<IoUringSocket> self = parent_.removeSocket(fd_);
    }
  } else {
    ASSERT(false);
  }
}

void IoUringAcceptSocket::start() {
  submitRequest();
}

void IoUringAcceptSocket::close() {
  if (accept_req_ != nullptr) {
    ENVOY_LOG(debug, "submit cancel request for the accept");
    cancel_req_ = new Request();
    cancel_req_->io_uring_socket_ = *this;
    cancel_req_->type_ = RequestType::Cancel;
    auto res = io_uring_impl_.prepareCancel(accept_req_, cancel_req_);
    if (res == Io::IoUringResult::Failed) {
      // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
      io_uring_impl_.submit();
      res = io_uring_impl_.prepareCancel(accept_req_, cancel_req_);
      RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare cancel");
    }
  }

  ENVOY_LOG(debug, "submit close request for the accept");
  close_req_ = new Request();
  close_req_->io_uring_socket_ = *this;
  close_req_->type_ = RequestType::Close;
  auto res = io_uring_impl_.prepareClose(fd_, close_req_);
  if (res == Io::IoUringResult::Failed) {
     // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    io_uring_impl_.submit();
    res = io_uring_impl_.prepareClose(fd_, close_req_);
    RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare close");
  }
  io_uring_impl_.submit();
}

void IoUringAcceptSocket::submitRequest() {
  accept_req_ = new Request();
  accept_req_->type_ = RequestType::Accept;
  accept_req_->io_uring_socket_ = *this;
  auto res = io_uring_impl_.prepareAccept(fd_, &remote_addr_, &remote_addr_len_, accept_req_);
  if (res == Io::IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    io_uring_impl_.submit();
    res = io_uring_impl_.prepareAccept(fd_, &remote_addr_, &remote_addr_len_, accept_req_);
    RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare accept");
  }
  ENVOY_LOG(debug, "Submit new accept request");
  io_uring_impl_.submit();
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
      req->io_uring_socket_->get().onRequestCompeltion(*req, result);
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

IoUringWorkerImpl::IoUringWorkerImpl(uint32_t io_uring_size, bool use_submission_queue_polling) :
    io_uring_impl_(io_uring_size, use_submission_queue_polling) { }

void IoUringWorkerImpl::start(Event::Dispatcher& dispatcher) {
  // This means already registered the file event.
  if (file_event_ != nullptr) {
    return;
  }

  const os_fd_t event_fd = io_uring_impl_.registerEventfd();
  // We only care about the read event of Eventfd, since we only receive the
  // event here.
  file_event_ = dispatcher.createFileEvent(
      event_fd, [this](uint32_t) { onFileEvent(); }, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
}

IoUring& IoUringWorkerImpl::get() {
  return io_uring_impl_;
}

void IoUringWorkerImpl::addAcceptSocket(os_fd_t fd, IoUringHandler& handler) {
  std::unique_ptr<IoUringAcceptSocket> socket = std::make_unique<IoUringAcceptSocket>(fd, io_uring_impl_, handler, *this);
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

} // namespace Io
} // namespace Envoy