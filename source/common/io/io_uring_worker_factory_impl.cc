#include "source/common/io/io_uring_worker_factory_impl.h"

#include "source/common/io/io_uring_worker_impl.h"

namespace Envoy {
namespace Io {

IoUringWorkerFactoryImpl::IoUringWorkerFactoryImpl(
    uint32_t io_uring_size, bool use_submission_queue_polling, uint32_t read_buffer_size,
    uint32_t write_timeout_ms, uint32_t write_high_watermark_bytes,
    uint32_t write_low_watermark_bytes, ThreadLocal::SlotAllocator& tls)
    : io_uring_size_(io_uring_size), use_submission_queue_polling_(use_submission_queue_polling),
      read_buffer_size_(read_buffer_size), write_timeout_ms_(write_timeout_ms),
      write_high_watermark_bytes_(write_high_watermark_bytes),
      write_low_watermark_bytes_(write_low_watermark_bytes), tls_(tls) {}

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
            read_buffer_size = read_buffer_size_, write_timeout_ms = write_timeout_ms_,
            write_high_watermark_bytes = write_high_watermark_bytes_,
            write_low_watermark_bytes = write_low_watermark_bytes_](Event::Dispatcher& dispatcher) {
    return std::make_shared<IoUringWorkerImpl>(
        io_uring_size, use_submission_queue_polling, read_buffer_size, write_timeout_ms,
        write_high_watermark_bytes, write_low_watermark_bytes, dispatcher);
  });
}

bool IoUringWorkerFactoryImpl::currentThreadRegistered() {
  return !tls_.isShutdown() && tls_.currentThreadRegistered();
}

} // namespace Io
} // namespace Envoy
