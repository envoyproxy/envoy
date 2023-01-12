#include "source/common/io/io_uring_worker.h"

#include <sys/socket.h>

#include "io_uring_impl.h"

namespace Envoy {
namespace Io {

IoUringSocketEntry::IoUringSocketEntry(os_fd_t fd, IoUringWorkerImpl& parent)
    : fd_(fd), parent_(parent) {}

std::unique_ptr<IoUringSocketEntry> IoUringSocketEntry::unlink() {
  return parent_.removeSocket(*this);
}

IoUringWorkerImpl::IoUringWorkerImpl(uint32_t io_uring_size, bool use_submission_queue_polling,
                                     Event::Dispatcher& dispatcher)
    : io_uring_impl_(io_uring_size, use_submission_queue_polling), dispatcher_(dispatcher) {}

IoUringWorkerImpl::~IoUringWorkerImpl() {
  ENVOY_LOG(trace, "destruct io uring worker");
  dispatcher_.clearDeferredDeleteList();
}

IoUringSocket& IoUringWorkerImpl::addAcceptSocket(os_fd_t fd, IoUringHandler&) {
  ENVOY_LOG(trace, "add accept socket, fd = {}", fd);
  PANIC("not implemented");
}

IoUringSocket& IoUringWorkerImpl::addServerSocket(os_fd_t fd, IoUringHandler&, uint32_t) {
  ENVOY_LOG(trace, "add server socket, fd = {}", fd);
  PANIC("not implemented");
}

IoUringSocket& IoUringWorkerImpl::addClientSocket(os_fd_t fd, IoUringHandler&, uint32_t) {
  ENVOY_LOG(trace, "add client socket, fd = {}", fd);
  PANIC("not implemented");
}

Event::Dispatcher& IoUringWorkerImpl::dispatcher() { return dispatcher_; }

Request* IoUringWorkerImpl::submitAcceptRequest(IoUringSocket& socket,
                                                sockaddr_storage* remote_addr,
                                                socklen_t* remote_addr_len) {
  Request* req = new Request{RequestType::Accept, socket};

  ENVOY_LOG(trace, "submit accept request, fd = {}, accept req = {}", socket.fd(), fmt::ptr(req));

  *remote_addr_len = sizeof(sockaddr_storage);
  auto res = io_uring_impl_.prepareAccept(
      socket.fd(), reinterpret_cast<struct sockaddr*>(remote_addr), remote_addr_len, req);
  if (res == Io::IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    submit();
    res = io_uring_impl_.prepareAccept(socket.fd(), reinterpret_cast<struct sockaddr*>(remote_addr),
                                       remote_addr_len, req);
    RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare accept");
  }
  submit();
  return req;
}

Request* IoUringWorkerImpl::submitCancelRequest(IoUringSocket& socket, Request* request_to_cancel) {
  Request* req = new Request{RequestType::Cancel, socket};

  ENVOY_LOG(trace, "submit cancel request, fd = {}, cancel req = {}", socket.fd(), fmt::ptr(req));

  auto res = io_uring_impl_.prepareCancel(request_to_cancel, req);
  if (res == Io::IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    submit();
    res = io_uring_impl_.prepareCancel(request_to_cancel, req);
    RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare cancel");
  }
  submit();
  return req;
}

Request* IoUringWorkerImpl::submitCloseRequest(IoUringSocket& socket) {
  Request* req = new Request{RequestType::Close, socket};

  ENVOY_LOG(trace, "submit close request, fd = {}, close req = {}", socket.fd(), fmt::ptr(req));

  auto res = io_uring_impl_.prepareClose(socket.fd(), req);
  if (res == Io::IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    submit();
    res = io_uring_impl_.prepareClose(socket.fd(), req);
    RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare close");
  }
  submit();
  return req;
}

Request* IoUringWorkerImpl::submitReadRequest(IoUringSocket& socket, struct iovec* iov) {
  Request* req = new Request{RequestType::Read, socket};

  ENVOY_LOG(trace, "submit read request, fd = {}, read req = {}", socket.fd(), fmt::ptr(req));

  auto res = io_uring_impl_.prepareReadv(socket.fd(), iov, 1, 0, req);
  if (res == Io::IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    submit();
    res = io_uring_impl_.prepareReadv(socket.fd(), iov, 1, 0, req);
    RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare readv");
  }
  submit();
  return req;
}

Request* IoUringWorkerImpl::submitWritevRequest(IoUringSocket& socket, struct iovec* iovecs,
                                                uint64_t num_vecs) {
  Request* req = new Request{RequestType::Write, socket};

  ENVOY_LOG(trace, "submit write request, fd = {}, req = {}", socket.fd(), fmt::ptr(req));

  auto res = io_uring_impl_.prepareWritev(socket.fd(), iovecs, num_vecs, 0, req);
  if (res == Io::IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    submit();
    res = io_uring_impl_.prepareWritev(socket.fd(), iovecs, num_vecs, 0, req);
    RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare writev");
  }
  submit();
  return req;
}

Request*
IoUringWorkerImpl::submitConnectRequest(IoUringSocket& socket,
                                        const Network::Address::InstanceConstSharedPtr& address) {
  Request* req = new Request{RequestType::Connect, socket};

  ENVOY_LOG(trace, "submit connect request, fd = {}, req = {}", socket.fd(), fmt::ptr(req));

  auto res = io_uring_impl_.prepareConnect(socket.fd(), address, req);
  if (res == Io::IoUringResult::Failed) {
    // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
    submit();
    res = io_uring_impl_.prepareConnect(socket.fd(), address, req);
    RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare writev");
  }
  submit();
  return req;
}

void IoUringWorkerImpl::onFileEvent() {
  ENVOY_LOG(trace, "io uring worker, on file event");
  delay_submit_ = true;
  io_uring_impl_.forEveryCompletion([](void* user_data, int32_t result) {
    auto req = static_cast<Io::Request*>(user_data);

    ENVOY_LOG(debug, "receive request completion, result = {}, req = {}", result, fmt::ptr(req));

    switch (req->type_) {
    case RequestType::Accept:
      ENVOY_LOG(trace, "receive accept request completion, fd = {}", req->io_uring_socket_.fd());
      req->io_uring_socket_.onAccept(result);
      break;
    case RequestType::Connect:
      ENVOY_LOG(trace, "receive connect request completion, fd = {}", req->io_uring_socket_.fd());
      req->io_uring_socket_.onConnect(result);
      break;
    case RequestType::Read:
      ENVOY_LOG(trace, "receive Read request completion, fd = {}", req->io_uring_socket_.fd());
      req->io_uring_socket_.onRead(result);
      break;
    case RequestType::Write:
      ENVOY_LOG(trace, "receive write request completion, fd = {}", req->io_uring_socket_.fd());
      req->io_uring_socket_.onWrite(result);
      break;
    case RequestType::Close:
      ENVOY_LOG(trace, "receive close request completion, fd = {}", req->io_uring_socket_.fd());
      req->io_uring_socket_.onClose(result);
      break;
    case RequestType::Cancel:
      ENVOY_LOG(trace, "receive cancel request completion, fd = {}", req->io_uring_socket_.fd());
      req->io_uring_socket_.onCancel(result);
      break;
    }

    delete req;
  });
  delay_submit_ = false;
  submit();
}

void IoUringWorkerImpl::submit() {
  if (!delay_submit_) {
    io_uring_impl_.submit();
  }
}

std::unique_ptr<IoUringSocketEntry> IoUringWorkerImpl::removeSocket(IoUringSocketEntry& socket) {
  return socket.removeFromList(sockets_);
}

} // namespace Io
} // namespace Envoy
