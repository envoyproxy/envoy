#include "source/common/io/io_uring_factory.h"

#include "source/common/io/io_uring_worker.h"

namespace Envoy {
namespace Io {

IoUringFactoryImpl::IoUringFactoryImpl(uint32_t io_uring_size, bool use_submission_queue_polling,
                                       ThreadLocal::SlotAllocator& tls)
    : io_uring_size_(io_uring_size), use_submission_queue_polling_(use_submission_queue_polling),
      tls_(tls) {}

OptRef<IoUring> IoUringFactoryImpl::get() const {
  auto ret = tls_.get();
  if (ret == absl::nullopt) {
    return absl::nullopt;
  }
  return ret.ref().get();
}

void IoUringFactoryImpl::onServerInitialized() {
  tls_.set([io_uring_size = io_uring_size_,
            use_submission_queue_polling = use_submission_queue_polling_](Event::Dispatcher&) {
    return std::make_shared<IoUringWorkerImpl>(io_uring_size, use_submission_queue_polling);
  });
}

bool IoUringFactoryImpl::currentThreadRegistered() { return tls_.currentThreadRegistered(); }

FileEventAdapter& IoUringFactoryImpl::getFileEventAdapter() {
  auto ret = tls_.get();
  return ret.ref().getFileEventAdapter();
}

} // namespace Io
} // namespace Envoy