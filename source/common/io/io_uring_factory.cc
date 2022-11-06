#include "source/common/io/io_uring_factory.h"

#include "source/common/io/io_uring_worker.h"

namespace Envoy {
namespace Io {

IoUringFactoryImpl::IoUringFactoryImpl(uint32_t io_uring_size, bool use_submission_queue_polling,
                                       ThreadLocal::SlotAllocator& tls)
    : io_uring_size_(io_uring_size), use_submission_queue_polling_(use_submission_queue_polling),
      tls_(tls) {}

OptRef<IoUringWorker> IoUringFactoryImpl::getIoUringWorker() const {
  auto ret = tls_.get();
  if (ret == absl::nullopt) {
    return absl::nullopt;
  }
  return ret.ref();
}

OptRef<IoUring> IoUringFactoryImpl::get() const {
  auto ret = tls_.get();
  if (ret == absl::nullopt) {
    return absl::nullopt;
  }
  return ret.ref().get();
}

void IoUringFactoryImpl::onServerInitialized() {
  tls_.set([io_uring_size = io_uring_size_,
            use_submission_queue_polling = use_submission_queue_polling_](Event::Dispatcher& dispatcher) {
    auto io_uring_worker = std::make_shared<IoUringWorkerImpl>(io_uring_size, use_submission_queue_polling, dispatcher);
    io_uring_worker->start();
    return io_uring_worker;
  });
}

bool IoUringFactoryImpl::currentThreadRegistered() { return tls_.currentThreadRegistered(); }

} // namespace Io
} // namespace Envoy