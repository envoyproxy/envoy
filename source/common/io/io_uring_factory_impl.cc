#include "source/common/io/io_uring_factory_impl.h"

#include "source/common/io/io_uring_worker_impl.h"

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

void IoUringFactoryImpl::onServerInitialized() {
  tls_.set([io_uring_size = io_uring_size_,
            use_submission_queue_polling =
                use_submission_queue_polling_](Event::Dispatcher& dispatcher) {
    return std::make_shared<IoUringWorkerImpl>(io_uring_size, use_submission_queue_polling,
                                               dispatcher);
  });
}

bool IoUringFactoryImpl::currentThreadRegistered() { return tls_.currentThreadRegistered(); }

} // namespace Io
} // namespace Envoy
