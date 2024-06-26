#include "source/common/io/io_uring_impl.h"

#include <sys/eventfd.h>

namespace Envoy {
namespace Io {

bool isIoUringSupported() {
  struct io_uring_params p {};
  struct io_uring ring;

  bool is_supported = io_uring_queue_init_params(2, &ring, &p) == 0;
  if (is_supported) {
    io_uring_queue_exit(&ring);
  }

  return is_supported;
}

IoUringImpl::IoUringImpl(uint32_t io_uring_size, bool use_submission_queue_polling)
    : cqes_(io_uring_size, nullptr) {
  struct io_uring_params p {};
  if (use_submission_queue_polling) {
    p.flags |= IORING_SETUP_SQPOLL;
  }
  // TODO (soulxu): According to the man page: `By default, the CQ ring will have twice the number
  // of entries as specified by entries for the SQ ring`. But currently we only use the same size
  // with SQ ring. We will figure out better handle of entries number in the future.
  int ret = io_uring_queue_init_params(io_uring_size, &ring_, &p);
  RELEASE_ASSERT(ret == 0, fmt::format("unable to initialize io_uring: {}", errorDetails(-ret)));
}

IoUringImpl::~IoUringImpl() { io_uring_queue_exit(&ring_); }

os_fd_t IoUringImpl::registerEventfd() {
  ASSERT(!isEventfdRegistered());
  // Mark the eventfd as non-blocking. since after injected completion is added. the eventfd
  // will be activated to trigger the event callback. For the case of only injected completion
  // is added and no actual iouring event. Then non-blocking can avoid the reading of eventfd
  // blocking.
  event_fd_ = eventfd(0, EFD_NONBLOCK);
  int res = io_uring_register_eventfd(&ring_, event_fd_);
  RELEASE_ASSERT(res == 0, fmt::format("unable to register eventfd: {}", errorDetails(-res)));
  return event_fd_;
}

void IoUringImpl::unregisterEventfd() {
  ASSERT(isEventfdRegistered());
  int res = io_uring_unregister_eventfd(&ring_);
  RELEASE_ASSERT(res == 0, fmt::format("unable to unregister eventfd: {}", errorDetails(-res)));
  SET_SOCKET_INVALID(event_fd_);
}

bool IoUringImpl::isEventfdRegistered() const { return SOCKET_VALID(event_fd_); }

void IoUringImpl::forEveryCompletion(const CompletionCb& completion_cb) {
  ASSERT(SOCKET_VALID(event_fd_));

  eventfd_t v;
  while (true) {
    int ret = eventfd_read(event_fd_, &v);
    if (ret != 0) {
      ASSERT(errno == EAGAIN);
      break;
    }
  }

  unsigned count = io_uring_peek_batch_cqe(&ring_, cqes_.data(), cqes_.size());

  for (unsigned i = 0; i < count; ++i) {
    struct io_uring_cqe* cqe = cqes_[i];
    completion_cb(reinterpret_cast<Request*>(cqe->user_data), cqe->res, false);
  }

  io_uring_cq_advance(&ring_, count);

  ENVOY_LOG(trace, "the num of injected completion is {}", injected_completions_.size());
  // TODO(soulxu): Add bound here to avoid too many completion to stuck the thread too
  // long.
  // Iterate the injected completion.
  while (!injected_completions_.empty()) {
    auto& completion = injected_completions_.front();
    completion_cb(completion.user_data_, completion.result_, true);
    // The socket may closed in the completion_cb and all the related completions are
    // removed.
    if (injected_completions_.empty()) {
      break;
    }
    injected_completions_.pop_front();
  }
}

IoUringResult IoUringImpl::prepareAccept(os_fd_t fd, struct sockaddr* remote_addr,
                                         socklen_t* remote_addr_len, Request* user_data) {
  ENVOY_LOG(trace, "prepare close for fd = {}", fd);
  // TODO (soulxu): Handling the case of CQ ring is overflow.
  ASSERT(!(*(ring_.sq.kflags) & IORING_SQ_CQ_OVERFLOW));
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
  if (sqe == nullptr) {
    return IoUringResult::Failed;
  }

  io_uring_prep_accept(sqe, fd, remote_addr, remote_addr_len, 0);
  io_uring_sqe_set_data(sqe, user_data);
  return IoUringResult::Ok;
}

IoUringResult IoUringImpl::prepareConnect(os_fd_t fd,
                                          const Network::Address::InstanceConstSharedPtr& address,
                                          Request* user_data) {
  ENVOY_LOG(trace, "prepare connect for fd = {}", fd);
  // TODO (soulxu): Handling the case of CQ ring is overflow.
  ASSERT(!(*(ring_.sq.kflags) & IORING_SQ_CQ_OVERFLOW));
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
  if (sqe == nullptr) {
    return IoUringResult::Failed;
  }

  io_uring_prep_connect(sqe, fd, address->sockAddr(), address->sockAddrLen());
  io_uring_sqe_set_data(sqe, user_data);
  return IoUringResult::Ok;
}

IoUringResult IoUringImpl::prepareReadv(os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs,
                                        off_t offset, Request* user_data) {
  ENVOY_LOG(trace, "prepare readv for fd = {}", fd);
  // TODO (soulxu): Handling the case of CQ ring is overflow.
  ASSERT(!(*(ring_.sq.kflags) & IORING_SQ_CQ_OVERFLOW));
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
  if (sqe == nullptr) {
    return IoUringResult::Failed;
  }

  io_uring_prep_readv(sqe, fd, iovecs, nr_vecs, offset);
  io_uring_sqe_set_data(sqe, user_data);
  return IoUringResult::Ok;
}

IoUringResult IoUringImpl::prepareWritev(os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs,
                                         off_t offset, Request* user_data) {
  ENVOY_LOG(trace, "prepare writev for fd = {}", fd);
  // TODO (soulxu): Handling the case of CQ ring is overflow.
  ASSERT(!(*(ring_.sq.kflags) & IORING_SQ_CQ_OVERFLOW));
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
  if (sqe == nullptr) {
    return IoUringResult::Failed;
  }

  io_uring_prep_writev(sqe, fd, iovecs, nr_vecs, offset);
  io_uring_sqe_set_data(sqe, user_data);
  return IoUringResult::Ok;
}

IoUringResult IoUringImpl::prepareClose(os_fd_t fd, Request* user_data) {
  ENVOY_LOG(trace, "prepare close for fd = {}", fd);
  // TODO (soulxu): Handling the case of CQ ring is overflow.
  ASSERT(!(*(ring_.sq.kflags) & IORING_SQ_CQ_OVERFLOW));
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
  if (sqe == nullptr) {
    return IoUringResult::Failed;
  }

  io_uring_prep_close(sqe, fd);
  io_uring_sqe_set_data(sqe, user_data);
  return IoUringResult::Ok;
}

IoUringResult IoUringImpl::prepareCancel(Request* cancelling_user_data, Request* user_data) {
  ENVOY_LOG(trace, "prepare cancels for user data = {}", fmt::ptr(cancelling_user_data));
  // TODO (soulxu): Handling the case of CQ ring is overflow.
  ASSERT(!(*(ring_.sq.kflags) & IORING_SQ_CQ_OVERFLOW));
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
  if (sqe == nullptr) {
    ENVOY_LOG(trace, "failed to prepare cancel for user data = {}", fmt::ptr(cancelling_user_data));
    return IoUringResult::Failed;
  }

  io_uring_prep_cancel(sqe, cancelling_user_data, 0);
  io_uring_sqe_set_data(sqe, user_data);
  return IoUringResult::Ok;
}

IoUringResult IoUringImpl::prepareShutdown(os_fd_t fd, int how, Request* user_data) {
  ENVOY_LOG(trace, "prepare shutdown for fd = {}, how = {}", fd, how);
  // TODO (soulxu): Handling the case of CQ ring is overflow.
  ASSERT(!(*(ring_.sq.kflags) & IORING_SQ_CQ_OVERFLOW));
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
  if (sqe == nullptr) {
    ENVOY_LOG(trace, "failed to prepare shutdown for fd = {}", fd);
    return IoUringResult::Failed;
  }

  io_uring_prep_shutdown(sqe, fd, how);
  io_uring_sqe_set_data(sqe, user_data);
  return IoUringResult::Ok;
}

IoUringResult IoUringImpl::submit() {
  int res = io_uring_submit(&ring_);
  RELEASE_ASSERT(res >= 0 || res == -EBUSY, "unable to submit io_uring queue entries");
  return res == -EBUSY ? IoUringResult::Busy : IoUringResult::Ok;
}

void IoUringImpl::injectCompletion(os_fd_t fd, Request* user_data, int32_t result) {
  injected_completions_.emplace_back(fd, user_data, result);
  ENVOY_LOG(trace, "inject completion, fd = {}, req = {}, num injects = {}", fd,
            fmt::ptr(user_data), injected_completions_.size());
}

void IoUringImpl::removeInjectedCompletion(os_fd_t fd) {
  ENVOY_LOG(trace, "remove injected completions for fd = {}, size = {}", fd,
            injected_completions_.size());
  injected_completions_.remove_if([fd](InjectedCompletion& completion) {
    if (fd == completion.fd_) {
      // Release the user data before remove this completion.
      delete reinterpret_cast<Request*>(completion.user_data_);
    }
    return fd == completion.fd_;
  });
}

} // namespace Io
} // namespace Envoy
