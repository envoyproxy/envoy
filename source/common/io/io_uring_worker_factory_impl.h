#pragma once

#include "envoy/common/io/io_uring.h"
#include "envoy/thread_local/thread_local.h"

namespace Envoy {
namespace Io {

class IoUringWorkerFactoryImpl : public IoUringWorkerFactory {
public:
  IoUringWorkerFactoryImpl(uint32_t io_uring_size, bool use_submission_queue_polling,
                           uint32_t read_buffer_size, uint32_t write_timeout_ms,
                           ThreadLocal::SlotAllocator& tls);

  OptRef<IoUringWorker> getIoUringWorker() override;

  void onWorkerThreadInitialized() override;
  bool currentThreadRegistered() override;

private:
  const uint32_t io_uring_size_;
  const bool use_submission_queue_polling_;
  const uint32_t read_buffer_size_;
  const uint32_t write_timeout_ms_;
  ThreadLocal::TypedSlot<IoUringWorker> tls_;
};

} // namespace Io
} // namespace Envoy
