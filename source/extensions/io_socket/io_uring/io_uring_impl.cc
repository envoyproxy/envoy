#include "source/extensions/io_socket/io_uring/io_uring_impl.h"

#include <sys/eventfd.h>

namespace Envoy {
namespace Extensions {
namespace IoSocket {
namespace IoUring {

IoUringFactoryImpl::IoUringFactoryImpl(const uint32_t io_uring_size,
                                       const bool use_submission_queue_polling)
    : io_uring_size_(io_uring_size), use_submission_queue_polling_(use_submission_queue_polling) {}

IoUring& IoUringFactoryImpl::getOrCreateUring() const {
  static thread_local IoUringImpl uring(io_uring_size_, use_submission_queue_polling_);
  return uring;
}

IoUringImpl::IoUringImpl(const uint32_t io_uring_size, const bool use_submission_queue_polling)
    : io_uring_size_(io_uring_size), cqes_(io_uring_size_, nullptr) {
  unsigned flags{0};
  if (use_submission_queue_polling) {
    flags |= IORING_SETUP_SQPOLL;
  }
  int ret = io_uring_queue_init(io_uring_size_, &ring_, flags);
  RELEASE_ASSERT(ret == 0, fmt::format("Unable to initialize io_uring: {}", errorDetails(-ret)));
}

IoUringImpl::~IoUringImpl() { io_uring_queue_exit(&ring_); }

os_fd_t IoUringImpl::registerEventfd() {
  ASSERT(!is_eventfd_registered_);
  os_fd_t event_fd = eventfd(0, 0);
  int res = io_uring_register_eventfd(&ring_, event_fd);
  RELEASE_ASSERT(res == 0, fmt::format("unable to register eventfd: {}", errorDetails(-res)));
  is_eventfd_registered_ = true;
  return event_fd;
}

void IoUringImpl::unregisterEventfd() {
  int res = io_uring_unregister_eventfd(&ring_);
  RELEASE_ASSERT(res == 0, fmt::format("unable to unregister eventfd: {}", errorDetails(-res)));
  is_eventfd_registered_ = false;
}

bool IoUringImpl::isEventfdRegistered() const { return is_eventfd_registered_; }

void IoUringImpl::forEveryCompletion(std::function<void(Request&, int32_t)> completion_cb) {
  unsigned count = io_uring_peek_batch_cqe(&ring_, cqes_.data(), io_uring_size_);

  for (unsigned i = 0; i < count; ++i) {
    struct io_uring_cqe* cqe = cqes_[i];
    RELEASE_ASSERT(cqe->res >= 0, fmt::format("async request failed: {}", errorDetails(-cqe->res)));
    auto req = reinterpret_cast<Request*>(cqe->user_data);
    completion_cb(*req, cqe->res);
    delete req;
  }
  io_uring_cq_advance(&ring_, count);
}

void IoUringImpl::prepareAccept(os_fd_t fd, struct sockaddr* remote_addr,
                                socklen_t* remote_addr_len) {
  struct io_uring_sqe* sqe = getSqe();
  io_uring_prep_accept(sqe, fd, remote_addr, remote_addr_len, 0);
  auto req = new Request{absl::nullopt, RequestType::Accept};
  io_uring_sqe_set_data(sqe, req);
}

void IoUringImpl::prepareConnect(os_fd_t fd, IoUringSocketHandleImpl& iohandle,
                                 const Network::Address::InstanceConstSharedPtr& address) {
  struct io_uring_sqe* sqe = getSqe();
  io_uring_prep_connect(sqe, fd, address->sockAddr(), address->sockAddrLen());
  auto req = new Request{iohandle, RequestType::Connect};
  io_uring_sqe_set_data(sqe, req);
}

void IoUringImpl::prepareRead(os_fd_t fd, IoUringSocketHandleImpl& iohandle, struct iovec* iov) {
  struct io_uring_sqe* sqe = getSqe();
  io_uring_prep_readv(sqe, fd, iov, 1, 0);
  auto req = new Request{iohandle, RequestType::Read};
  io_uring_sqe_set_data(sqe, req);
}

void IoUringImpl::prepareWrite(os_fd_t fd, std::list<Buffer::SliceDataPtr>&& slices) {
  struct iovec* iov = new struct iovec[slices.size()];
  struct io_uring_sqe* sqe = getSqe();
  uint32_t count{0};
  for (auto& slice : slices) {
    absl::Span<uint8_t> mdata = slice->getMutableData();
    iov[count].iov_base = mdata.data();
    iov[count].iov_len = mdata.size();
    count++;
  }
  io_uring_prep_writev(sqe, fd, iov, slices.size(), 0);
  auto req = new Request{absl::nullopt, RequestType::Write, iov, std::move(slices)};
  io_uring_sqe_set_data(sqe, req);
}

void IoUringImpl::prepareClose(os_fd_t fd) {
  struct io_uring_sqe* sqe = getSqe();
  io_uring_prep_close(sqe, fd);
  auto req = new Request{absl::nullopt, RequestType::Close};
  io_uring_sqe_set_data(sqe, req);
}

void IoUringImpl::submit() { io_uring_submit(&ring_); }

struct io_uring_sqe* IoUringImpl::getSqe() {
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
  if (sqe == nullptr) {
    FANCY_LOG(warn, "unable to get a new SQE, submitting existing ones...");
    submit();
    sqe = io_uring_get_sqe(&ring_);
  }
  RELEASE_ASSERT(sqe != nullptr, "unable to get SQE");
  return sqe;
}

} // namespace IoUring
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
