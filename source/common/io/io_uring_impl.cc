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

IoUringImpl::IoUringImpl(uint32_t io_uring_size, bool use_submission_queue_polling) {
  struct io_uring_params p {};

  // Performance flags for Envoy's per-worker-thread model.
#ifdef IORING_SETUP_COOP_TASKRUN
  p.flags |= IORING_SETUP_COOP_TASKRUN;
#endif
#ifdef IORING_SETUP_SINGLE_ISSUER
  p.flags |= IORING_SETUP_SINGLE_ISSUER;
#endif
#ifdef IORING_SETUP_DEFER_TASKRUN
  p.flags |= IORING_SETUP_DEFER_TASKRUN;
#endif

  // Size CQ ring at 2x SQ to reduce overflow risk.
  p.flags |= IORING_SETUP_CQSIZE;
  p.cq_entries = io_uring_size * 2;

  if (use_submission_queue_polling) {
    p.flags |= IORING_SETUP_SQPOLL;
    p.sq_thread_idle = 100;
  }

  int ret = io_uring_queue_init_params(io_uring_size, &ring_, &p);
  if (ret == -EINVAL) {
    // Fallback: retry without newer flags for older kernels (< 5.19).
    p.flags &= ~static_cast<unsigned>(IORING_SETUP_CQSIZE);
    p.cq_entries = 0;
#ifdef IORING_SETUP_COOP_TASKRUN
    p.flags &= ~static_cast<unsigned>(IORING_SETUP_COOP_TASKRUN);
#endif
#ifdef IORING_SETUP_SINGLE_ISSUER
    p.flags &= ~static_cast<unsigned>(IORING_SETUP_SINGLE_ISSUER);
#endif
#ifdef IORING_SETUP_DEFER_TASKRUN
    p.flags &= ~static_cast<unsigned>(IORING_SETUP_DEFER_TASKRUN);
#endif
    ret = io_uring_queue_init_params(io_uring_size, &ring_, &p);
  }
  RELEASE_ASSERT(ret >= 0, fmt::format("unable to initialize io_uring: {}", errorDetails(-ret)));

  // Size the `CQE` vector to match the actual CQ ring size.
  cqes_.resize(ring_.cq.ring_sz > 0 ? *ring_.cq.kring_entries : io_uring_size, nullptr);
}

IoUringImpl::~IoUringImpl() { io_uring_queue_exit(&ring_); }

os_fd_t IoUringImpl::registerEventfd() {
  ASSERT(!isEventfdRegistered());
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

bool IoUringImpl::checkCqOverflow() {
  if (*(ring_.sq.kflags) & IORING_SQ_CQ_OVERFLOW) {
    cq_overflow_count_++;
    ENVOY_LOG(warn, "io_uring CQ overflow detected (count={})", cq_overflow_count_);
    return true;
  }
  return false;
}

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

  // Check for CQ overflow before draining.
  checkCqOverflow();

  unsigned count = io_uring_peek_batch_cqe(&ring_, cqes_.data(), cqes_.size());
  for (unsigned i = 0; i < count; ++i) {
    struct io_uring_cqe* cqe = cqes_[i];
    completion_cb(reinterpret_cast<Request*>(cqe->user_data), cqe->res, false);
  }
  io_uring_cq_advance(&ring_, count);

  ENVOY_LOG(trace, "number of injected completions: {}", injected_completions_.size());

  // Iterate injected completions with a bound to avoid starving the thread.
  static constexpr uint32_t kMaxInjectedPerIteration = 256;
  uint32_t injected_count = 0;
  while (!injected_completions_.empty() && injected_count < kMaxInjectedPerIteration) {
    auto& completion = injected_completions_.front();
    completion_cb(completion.user_data_, completion.result_, true);
    if (injected_completions_.empty()) {
      break;
    }
    injected_completions_.pop_front();
    ++injected_count;
  }
}

IoUringResult IoUringImpl::prepareAccept(os_fd_t fd, struct sockaddr* remote_addr,
                                         socklen_t* remote_addr_len, Request* user_data) {
  ENVOY_LOG(trace, "prepare accept for fd = {}", fd);
  checkCqOverflow();
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
  checkCqOverflow();
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
  checkCqOverflow();
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
  checkCqOverflow();
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
  if (sqe == nullptr) {
    return IoUringResult::Failed;
  }

  io_uring_prep_writev(sqe, fd, iovecs, nr_vecs, offset);
  io_uring_sqe_set_data(sqe, user_data);
  return IoUringResult::Ok;
}

IoUringResult IoUringImpl::prepareRecv(os_fd_t fd, void* buf, uint32_t len, int flags,
                                       Request* user_data) {
  ENVOY_LOG(trace, "prepare recv for fd = {}", fd);
  checkCqOverflow();
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
  if (sqe == nullptr) {
    return IoUringResult::Failed;
  }

  io_uring_prep_recv(sqe, fd, buf, len, flags);
  io_uring_sqe_set_data(sqe, user_data);
  return IoUringResult::Ok;
}

IoUringResult IoUringImpl::prepareSend(os_fd_t fd, const void* buf, uint32_t len, int flags,
                                       Request* user_data) {
  ENVOY_LOG(trace, "prepare send for fd = {}", fd);
  checkCqOverflow();
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
  if (sqe == nullptr) {
    return IoUringResult::Failed;
  }

  io_uring_prep_send(sqe, fd, buf, len, flags | MSG_NOSIGNAL);
  io_uring_sqe_set_data(sqe, user_data);
  return IoUringResult::Ok;
}

IoUringResult IoUringImpl::prepareClose(os_fd_t fd, Request* user_data) {
  ENVOY_LOG(trace, "prepare close for fd = {}", fd);
  checkCqOverflow();
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
  if (sqe == nullptr) {
    return IoUringResult::Failed;
  }

  io_uring_prep_close(sqe, fd);
  io_uring_sqe_set_data(sqe, user_data);
  return IoUringResult::Ok;
}

IoUringResult IoUringImpl::prepareCancel(Request* cancelling_user_data, Request* user_data) {
  ENVOY_LOG(trace, "prepare cancel for user data = {}", fmt::ptr(cancelling_user_data));
  checkCqOverflow();
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
  checkCqOverflow();
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
      delete reinterpret_cast<Request*>(completion.user_data_);
    }
    return fd == completion.fd_;
  });
}

} // namespace Io
} // namespace Envoy
