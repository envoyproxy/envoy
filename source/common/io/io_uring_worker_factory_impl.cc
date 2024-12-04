#include "source/common/io/io_uring_worker_factory_impl.h"

#include "source/common/io/io_uring_worker_impl.h"
#include "source/common/network/io_uring_socket_handle_impl.h"

namespace Envoy {
namespace Io {

IoUringWorkerFactoryImpl::IoUringWorkerFactoryImpl(uint32_t io_uring_size,
                                                   bool use_submission_queue_polling,
                                                   uint32_t read_buffer_size,
                                                   uint32_t write_timeout_ms,
                                                   ThreadLocal::SlotAllocator& tls)
    : io_uring_size_(io_uring_size), use_submission_queue_polling_(use_submission_queue_polling),
      read_buffer_size_(read_buffer_size), write_timeout_ms_(write_timeout_ms), tls_(tls) {}

OptRef<IoUringWorker> IoUringWorkerFactoryImpl::getIoUringWorker() {
  auto ret = tls_.get();
  if (ret == absl::nullopt) {
    return absl::nullopt;
  }
  return ret;
}

void IoUringWorkerFactoryImpl::onWorkerThreadInitialized() {
  tls_.set([io_uring_size = io_uring_size_,
            use_submission_queue_polling = use_submission_queue_polling_,
            read_buffer_size = read_buffer_size_,
            write_timeout_ms = write_timeout_ms_](Event::Dispatcher& dispatcher) {
    return std::make_shared<IoUringWorkerImpl>(io_uring_size, use_submission_queue_polling,
                                               read_buffer_size, write_timeout_ms, dispatcher);
  });
}

bool IoUringWorkerFactoryImpl::currentThreadRegistered() {
  return !tls_.isShutdown() && tls_.currentThreadRegistered();
}

Network::IoHandlePtr
IoUringWorkerFactoryImpl::createIoUringSocketHandle(int socket_fd, bool socket_v6only,
                                                    absl::optional<int> domain) {
  return std::make_unique<Network::IoUringSocketHandleImpl>(*this, socket_fd, socket_v6only,
                                                            domain);
}

} // namespace Io
} // namespace Envoy
