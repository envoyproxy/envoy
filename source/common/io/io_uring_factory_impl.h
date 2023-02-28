#pragma once

#include "envoy/common/io/io_uring.h"
#include "envoy/thread_local/thread_local.h"

namespace Envoy {
namespace Io {

class IoUringFactoryImpl : public IoUringFactory {
public:
  IoUringFactoryImpl(uint32_t io_uring_size, bool use_submission_queue_polling,
                     uint32_t accept_size, uint32_t read_buffer_size,
                     ThreadLocal::SlotAllocator& tls);

  OptRef<IoUringWorker> getIoUringWorker() override;

  void onServerInitialized() override;
  bool currentThreadRegistered() override;

private:
  const uint32_t io_uring_size_;
  const bool use_submission_queue_polling_;
  const uint32_t accept_size_;
  const uint32_t read_buffer_size_;
  ThreadLocal::TypedSlot<IoUringWorker> tls_;
};

} // namespace Io
} // namespace Envoy
