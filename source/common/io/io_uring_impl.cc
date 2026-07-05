#include "source/common/io/io_uring_impl.h"

#include <sys/eventfd.h>

#include <chrono>

namespace Envoy {
namespace Io {

namespace {
// The buffer group id for the provided buffer ring backing `multishot` reads. A single group is
// enough since each worker thread owns its own io_uring.
constexpr uint16_t ProvidedBufferGroupId = 0;

// Idle time in milliseconds before the SQPOLL kernel thread sleeps when the submission queue is
// empty.
constexpr uint32_t SqPollIdleMs = 100;

// Rounds the configured ring size up to the next power of two, as required by the provided buffer
// ring, capped so the number of provided buffers stays bounded.
uint32_t providedBufferCount(uint32_t io_uring_size) {
  constexpr uint32_t MaxProvidedBuffers = 4096;
  uint32_t count = 1;
  while (count < io_uring_size && count < MaxProvidedBuffers) {
    count <<= 1;
  }
  return count;
}
} // namespace

// A provided buffer ring registered with the kernel. The buffer memory is owned here and kept alive
// through the shared_ptr held by read fragments, while the kernel ring is released by the owning
// IoUringImpl before the io_uring is torn down.
class IoUringBufferPoolImpl : public IoUringBufferPool {
public:
  IoUringBufferPoolImpl(struct io_uring& ring, uint16_t group_id, uint32_t buffer_count,
                        uint32_t buffer_size)
      : ring_(&ring), group_id_(group_id), buffer_count_(buffer_count), buffer_size_(buffer_size),
        mask_(io_uring_buf_ring_mask(buffer_count)) {
    int err = 0;
    buf_ring_ = io_uring_setup_buf_ring(ring_, buffer_count_, group_id_, 0, &err);
    if (buf_ring_ == nullptr) {
      return;
    }
    // The kernel ring is registered, so allocate the backing memory and hand every buffer to it.
    buffers_ = std::make_unique<uint8_t[]>(static_cast<size_t>(buffer_count_) * buffer_size_);
    // The buffer id is a 16-bit field in io_uring, and buffer_count_ is capped well below that by
    // providedBufferCount. Iterate with the same width as buffer_count_ so the loop is bounded even
    // if the cap ever grows.
    for (uint32_t id = 0; id < buffer_count_; id++) {
      io_uring_buf_ring_add(buf_ring_, getBuffer(id), buffer_size_, static_cast<uint16_t>(id),
                            mask_, static_cast<int>(id));
    }
    io_uring_buf_ring_advance(buf_ring_, static_cast<int>(buffer_count_));
  }

  // Whether the kernel buffer ring was registered successfully. False on kernels that lack provided
  // buffer ring support.
  bool valid() const { return buf_ring_ != nullptr; }

  // Releases the kernel buffer ring while the io_uring is still alive. After this releaseBuffer is
  // a no-op, so outstanding read fragments can be drained safely once the ring is gone.
  void releaseRing() {
    if (buf_ring_ != nullptr) {
      io_uring_free_buf_ring(ring_, buf_ring_, buffer_count_, group_id_);
      buf_ring_ = nullptr;
    }
  }

  uint16_t groupId() const { return group_id_; }

  // IoUringBufferPool
  uint8_t* getBuffer(uint32_t buffer_id) override {
    return buffers_.get() + static_cast<size_t>(buffer_id) * buffer_size_;
  }
  uint32_t bufferSize() const override { return buffer_size_; }
  void releaseBuffer(const void* buffer) override {
    if (buf_ring_ == nullptr) {
      return;
    }
    // A stray pointer would index the ring out of bounds, so reject anything outside the backing
    // memory.
    const uint8_t* const buf_ptr = static_cast<const uint8_t*>(buffer);
    const uint8_t* const start = buffers_.get();
    const uint8_t* const end = start + static_cast<size_t>(buffer_count_) * buffer_size_;
    if (buf_ptr < start || buf_ptr >= end) {
      IS_ENVOY_BUG("released buffer pointer is out of range");
      return;
    }
    const auto buffer_id = static_cast<uint16_t>((buf_ptr - start) / buffer_size_);
    io_uring_buf_ring_add(buf_ring_, const_cast<void*>(buffer), buffer_size_, buffer_id, mask_, 0);
    io_uring_buf_ring_advance(buf_ring_, 1);
  }

private:
  struct io_uring* const ring_;
  struct io_uring_buf_ring* buf_ring_{nullptr};
  const uint16_t group_id_;
  const uint32_t buffer_count_;
  const uint32_t buffer_size_;
  const int mask_;
  std::unique_ptr<uint8_t[]> buffers_;
};

bool isIoUringSupported() {
  struct io_uring_params p {};
  struct io_uring ring;

  bool is_supported = io_uring_queue_init_params(2, &ring, &p) == 0;
  if (is_supported) {
    io_uring_queue_exit(&ring);
  }

  return is_supported;
}

IoUringImpl::IoUringImpl(uint32_t io_uring_size, bool use_submission_queue_polling,
                         bool enable_multishot_receive, uint32_t multishot_buffer_size) {
  struct io_uring_params p {};

  // Size the completion queue at twice the submission queue to reduce the chance of overflow.
  p.flags |= IORING_SETUP_CQSIZE;
  p.cq_entries = io_uring_size * 2;

  if (use_submission_queue_polling) {
    p.flags |= IORING_SETUP_SQPOLL;
    p.sq_thread_idle = SqPollIdleMs;
  }

  int ret = io_uring_queue_init_params(io_uring_size, &ring_, &p);
  if (ret == -EINVAL) {
    // `IORING_SETUP_CQSIZE` requires kernel 5.5 or newer. Retry without it on older kernels.
    p.flags &= ~static_cast<unsigned>(IORING_SETUP_CQSIZE);
    p.cq_entries = 0;
    ret = io_uring_queue_init_params(io_uring_size, &ring_, &p);
  }
  RELEASE_ASSERT(ret == 0, fmt::format("unable to initialize io_uring: {}", errorDetails(-ret)));

  // Size the completion vector to the submission queue, not the larger completion queue. Each read
  // completion re-arms one read, so reaping at most one submission queue worth of completions per
  // pass keeps those read re-arms within the submission queue. A larger burst is reaped across
  // later passes, which the worker re-arms while hasReadyCompletions stays true.
  cqes_.resize(io_uring_size, nullptr);

  // Set up the provided buffer ring for `multishot` reads when requested. A failure here means the
  // running kernel lacks provided buffer ring support, in which case `multishot` reads stay
  // disabled and the readv-based read path is used instead.
  if (enable_multishot_receive && multishot_buffer_size > 0) {
    const uint32_t buffer_count = providedBufferCount(io_uring_size);
    auto pool = std::make_shared<IoUringBufferPoolImpl>(ring_, ProvidedBufferGroupId, buffer_count,
                                                        multishot_buffer_size);
    if (pool->valid()) {
      buffer_pool_ = std::move(pool);
      ENVOY_LOG(debug, "io_uring multishot reads enabled, {} buffers of {} bytes", buffer_count,
                multishot_buffer_size);
    } else {
      ENVOY_LOG(debug, "io_uring multishot reads requested but provided buffer rings are "
                       "unsupported, falling back to readv");
    }
  }
}

IoUringImpl::~IoUringImpl() {
  // Release the provided buffer ring while the io_uring is still alive. The pool object itself may
  // outlive this ring when read fragments still reference its buffers, after which releaseBuffer is
  // a no-op.
  if (buffer_pool_ != nullptr) {
    buffer_pool_->releaseRing();
  }
  io_uring_queue_exit(&ring_);
}

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

void IoUringImpl::checkCqOverflow() {
  if (*(ring_.sq.kflags) & IORING_SQ_CQ_OVERFLOW) {
    cq_overflow_count_++;
    // Overflow persists under heavy load, so rate limit the warning to avoid flooding the log.
    ENVOY_LOG_PERIODIC(warn, std::chrono::seconds(1),
                       "io_uring completion queue overflow detected, count = {}",
                       cq_overflow_count_);
  }
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

  checkCqOverflow();
  unsigned count = io_uring_peek_batch_cqe(&ring_, cqes_.data(), cqes_.size());
  for (unsigned i = 0; i < count; ++i) {
    struct io_uring_cqe* cqe = cqes_[i];
    auto* req = reinterpret_cast<Request*>(cqe->user_data);
    if (req != nullptr) {
      // Surface the `multishot` completion metadata so the read path can recycle the provided
      // buffer and keep the request alive while the operation stays armed.
      req->setMoreCompletions((cqe->flags & IORING_CQE_F_MORE) != 0);
      req->setBufferId((cqe->flags & IORING_CQE_F_BUFFER)
                           ? static_cast<int32_t>(cqe->flags >> IORING_CQE_BUFFER_SHIFT)
                           : -1);
    }
    completion_cb(req, cqe->res, false);
  }
  io_uring_cq_advance(&ring_, count);

  ENVOY_LOG(trace, "the num of injected completion is {}", injected_completions_.size());
  // TODO(soulxu): Add bound here to avoid too many completion to stuck the thread too
  // long.
  // Iterate the injected completion.
  while (!injected_completions_.empty()) {
    auto completion = injected_completions_.front();
    injected_completions_.pop_front();
    completion_cb(completion.user_data_, completion.result_, true);
  }
}

bool IoUringImpl::hasReadyCompletions() const { return io_uring_cq_ready(&ring_) > 0; }

IoUringResult IoUringImpl::prepareAccept(os_fd_t fd, struct sockaddr* remote_addr,
                                         socklen_t* remote_addr_len, Request* user_data) {
  ENVOY_LOG(trace, "prepare accept for fd = {}", fd);
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
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
  if (sqe == nullptr) {
    return IoUringResult::Failed;
  }

  io_uring_prep_readv(sqe, fd, iovecs, nr_vecs, offset);
  io_uring_sqe_set_data(sqe, user_data);
  return IoUringResult::Ok;
}

bool IoUringImpl::isMultishotEnabled() const { return buffer_pool_ != nullptr; }

IoUringBufferPoolSharedPtr IoUringImpl::bufferPool() { return buffer_pool_; }

IoUringResult IoUringImpl::prepareReadMultishot(os_fd_t fd, Request* user_data) {
  ENVOY_LOG(trace, "prepare read multishot for fd = {}", fd);
  ASSERT(buffer_pool_ != nullptr);
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
  if (sqe == nullptr) {
    return IoUringResult::Failed;
  }

  io_uring_prep_recv_multishot(sqe, fd, nullptr, 0, 0);
  sqe->flags |= IOSQE_BUFFER_SELECT;
  io_uring_sqe_set_buf_group(sqe, buffer_pool_->groupId());
  io_uring_sqe_set_data(sqe, user_data);
  return IoUringResult::Ok;
}

IoUringResult IoUringImpl::prepareWritev(os_fd_t fd, const struct iovec* iovecs, unsigned nr_vecs,
                                         off_t offset, Request* user_data) {
  ENVOY_LOG(trace, "prepare writev for fd = {}", fd);
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
