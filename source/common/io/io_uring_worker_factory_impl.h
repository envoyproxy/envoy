#pragma once

#include "envoy/common/io/io_uring.h"
#include "envoy/thread_local/thread_local.h"

namespace Envoy {
namespace Io {

class IoUringWorkerFactoryImpl : public IoUringWorkerFactory {
public:
  IoUringWorkerFactoryImpl(uint32_t io_uring_size, bool use_submission_queue_polling,
                           bool enable_multishot_receive, uint32_t read_buffer_size,
                           uint32_t write_timeout_ms, uint32_t write_high_watermark_bytes,
                           uint32_t write_low_watermark_bytes, ThreadLocal::SlotAllocator& tls,
                           uint32_t provided_buffer_count = 0, bool enable_deferred_taskrun = false,
                           bool enable_receive_batching = false);

  OptRef<IoUringWorker> getIoUringWorker() override;

  void onWorkerThreadInitialized() override;
  bool currentThreadRegistered() override;

private:
  const uint32_t io_uring_size_;
  const bool use_submission_queue_polling_;
  const bool enable_multishot_receive_;
  const uint32_t read_buffer_size_;
  const uint32_t write_timeout_ms_;
  const uint32_t write_high_watermark_bytes_;
  const uint32_t write_low_watermark_bytes_;
  const uint32_t provided_buffer_count_;
  const bool enable_deferred_taskrun_;
  const bool enable_receive_batching_;
  ThreadLocal::TypedSlot<IoUringWorker> tls_;
};

} // namespace Io
} // namespace Envoy
