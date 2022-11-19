#include "source/common/io/io_uring_impl.h"

#include <sys/eventfd.h>

#include "source/common/common/utility.h"

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
    : io_uring_size_(io_uring_size), cqes_(io_uring_size_, nullptr) {
  struct io_uring_params p {};
  if (use_submission_queue_polling) {
    p.flags |= IORING_SETUP_SQPOLL;
  }
  int ret = io_uring_queue_init_params(io_uring_size_, &ring_, &p);
  RELEASE_ASSERT(ret == 0, fmt::format("unable to initialize io_uring: {}", errorDetails(-ret)));
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
  int res = io_uring_unregister_eventfd(&ring_);
  RELEASE_ASSERT(res == 0, fmt::format("unable to unregister eventfd: {}", errorDetails(-res)));
  SET_SOCKET_INVALID(event_fd_);
}

bool IoUringImpl::isEventfdRegistered() const { return SOCKET_VALID(event_fd_); }

void IoUringImpl::forEveryCompletion(CompletionCb completion_cb) {
  ASSERT(SOCKET_VALID(event_fd_));
  // move the current injected completions into temp vector in case
  // the event callback will inject new completion, then we should
  // go through those new completion in next round.
  std::vector<InjectedCompletion> current_injected_completions;
  current_injected_completions.swap(injected_completions_);

  eventfd_t v;
  while (true) {
    int ret = eventfd_read(event_fd_, &v);
    ENVOY_LOG(trace, "iteration every completion, ret = {}", ret);
    if (ret != 0) {
      ENVOY_LOG(trace, "iteration every completion, ret = {}, errno = {}", ret, errno);
      if (errno == EAGAIN) {
        break;
      } else {
        return;
      }
    }
  }

  unsigned count = io_uring_peek_batch_cqe(&ring_, cqes_.data(), io_uring_size_);

  for (unsigned i = 0; i < count; ++i) {
    struct io_uring_cqe* cqe = cqes_[i];
    completion_cb(reinterpret_cast<void*>(cqe->user_data), cqe->res);
  }

  ENVOY_LOG(trace, "has injected event, num = {}", injected_completions_.size());

  for (auto& completion : current_injected_completions) {
    completion_cb(completion.user_data_, completion.result_);
  }

  ENVOY_LOG(debug, "count = {}", count);
  io_uring_cq_advance(&ring_, count);
}

void IoUringImpl::injectCompletion(os_fd_t fd, void* user_data, int32_t result) {
  injected_completions_.emplace_back(fd, user_data, result);
  ENVOY_LOG(trace, "inject completion, fd = {}, req = {}, num injects = {}", fd, fmt::ptr(user_data), injected_completions_.size());
}

void IoUringImpl::removeInjectedCompletion(os_fd_t fd) {
  ENVOY_LOG(debug, "remove injected completion, fd = {}", fd);
  for(auto iter = injected_completions_.begin(); iter != injected_completions_.end(); ) {
    if (iter->fd_ == fd) {
      iter = injected_completions_.erase(iter);
    } else {
      iter++;
    }
  }
  ENVOY_LOG(debug, "remove injected completion done, fd = {}", fd);
}

IoUringResult IoUringImpl::prepareAccept(os_fd_t fd, struct sockaddr* remote_addr,
                                         socklen_t* remote_addr_len, void* user_data) {
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
                                          void* user_data) {
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
  if (sqe == nullptr) {
    return IoUringResult::Failed;
  }

  io_uring_prep_connect(sqe, fd, address->sockAddr(), address->sockAddrLen());
  io_uring_sqe_set_data(sqe, user_data);
  return IoUringResult::Ok;
}

IoUringResult IoUringImpl::prepareReadv(os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs,
                                        off_t offset, void* user_data) {
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
  if (sqe == nullptr) {
    return IoUringResult::Failed;
  }

  io_uring_prep_readv(sqe, fd, iovecs, nr_vecs, offset);
  io_uring_sqe_set_data(sqe, user_data);
  return IoUringResult::Ok;
}

IoUringResult IoUringImpl::prepareWritev(os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs,
                                         off_t offset, void* user_data) {
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
  if (sqe == nullptr) {
    return IoUringResult::Failed;
  }

  io_uring_prep_writev(sqe, fd, iovecs, nr_vecs, offset);
  io_uring_sqe_set_data(sqe, user_data);
  return IoUringResult::Ok;
}

IoUringResult IoUringImpl::prepareClose(os_fd_t fd, void* user_data) {
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
  if (sqe == nullptr) {
    return IoUringResult::Failed;
  }

  io_uring_prep_close(sqe, fd);
  io_uring_sqe_set_data(sqe, user_data);
  return IoUringResult::Ok;
}

IoUringResult IoUringImpl::prepareCancel(void* cancelling_user_data, void* user_data) {
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
  if (sqe == nullptr) {
    return IoUringResult::Failed;
  }

  io_uring_prep_cancel(sqe, cancelling_user_data, 0);
  io_uring_sqe_set_data(sqe, user_data);
  return IoUringResult::Ok;
}

IoUringResult IoUringImpl::submit() {
  int res = io_uring_submit(&ring_);
  RELEASE_ASSERT(res >= 0 || res == -EBUSY, "unable to submit io_uring queue entries");
  return res == -EBUSY ? IoUringResult::Busy : IoUringResult::Ok;
}

} // namespace Io
} // namespace Envoy
