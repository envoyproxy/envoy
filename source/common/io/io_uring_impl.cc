#include "source/common/io/io_uring_impl.h"

#include <sys/eventfd.h>

#include "envoy/extensions/io/io_uring/v3/io_uring.pb.h"
#include "envoy/extensions/io/io_uring/v3/io_uring.pb.validate.h"

#include "source/common/common/utility.h"

namespace Envoy {
namespace Io {

namespace {

constexpr uint32_t DefaultIoUringSize = 300;

} // namespace

thread_local bool IoUringFactoryImpl::is_instantiated_ = false;

IoUringFactoryImpl::IoUringFactoryImpl() {
  // TODO(rojkov): Currently only one factory per thread is supported which is
  // enough for networking, but future IO use cases may need to have multiple
  // factories supported with different settings: e.g. one factory for
  // networking and one more for block IO.
  RELEASE_ASSERT(!is_instantiated_, "only one io_uring per thread is supported now");
  is_instantiated_ = true;
}

IoUring& IoUringFactoryImpl::getOrCreate() const { return tls_->getTyped<IoUringImpl>(); }

Server::BootstrapExtensionPtr
IoUringFactoryImpl::createBootstrapExtension(const Protobuf::Message& config,
                                             Server::Configuration::ServerFactoryContext& context) {
  auto typed_config =
      MessageUtil::downcastAndValidate<const envoy::extensions::io::io_uring::v3::IoUring&>(
          config, context.messageValidationContext().staticValidationVisitor());
  io_uring_size_ = PROTOBUF_GET_WRAPPED_OR_DEFAULT(typed_config, io_uring_size, DefaultIoUringSize);
  use_submission_queue_polling_ = typed_config.use_submission_queue_polling();

  tls_ = context.threadLocal().allocateSlot();
  return std::make_unique<IoUringExtension>(*this);
}

void IoUringFactoryImpl::initialize() {
  tls_->set([io_uring_size = io_uring_size_,
             use_submission_queue_polling = use_submission_queue_polling_](Event::Dispatcher&) {
    return std::make_shared<IoUringImpl>(io_uring_size, use_submission_queue_polling);
  });
}

ProtobufTypes::MessagePtr IoUringFactoryImpl::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::io::io_uring::v3::IoUring>();
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
  event_fd_ = eventfd(0, 0);
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

  eventfd_t v;
  int ret = eventfd_read(event_fd_, &v);
  RELEASE_ASSERT(ret == 0, "unable to drain eventfd");

  unsigned count = io_uring_peek_batch_cqe(&ring_, cqes_.data(), io_uring_size_);

  for (unsigned i = 0; i < count; ++i) {
    struct io_uring_cqe* cqe = cqes_[i];
    completion_cb(reinterpret_cast<void*>(cqe->user_data), cqe->res);
  }
  io_uring_cq_advance(&ring_, count);
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

IoUringResult IoUringImpl::submit() {
  int res = io_uring_submit(&ring_);
  RELEASE_ASSERT(res >= 0 || res == -EBUSY, "unable to submit io_uring queue entries");
  return res == -EBUSY ? IoUringResult::Busy : IoUringResult::Ok;
}

REGISTER_FACTORY(IoUringFactoryImpl, Server::Configuration::BootstrapExtensionFactory);

} // namespace Io
} // namespace Envoy
