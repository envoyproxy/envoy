#pragma once

#include "envoy/thread_local/thread_local.h"

#include "source/common/io/io_uring.h"

namespace Envoy {
namespace Io {

class IoUringFactoryImpl : public IoUringFactory {
public:
  IoUringFactoryImpl(uint32_t io_uring_size, bool use_submission_queue_polling,
                     ThreadLocal::SlotAllocator& tls);

  OptRef<IoUringWorker> getIoUringWorker() const override;
  // IoUringFactory
  // TODO (soulxu): rename this method, it only about `get`.
  OptRef<IoUring> get() const override;
  void onServerInitialized() override;
  bool currentThreadRegistered() override;

private:
  const uint32_t io_uring_size_{};
  const bool use_submission_queue_polling_{};
  ThreadLocal::TypedSlot<IoUringWorker> tls_;
};

} // namespace Io
} // namespace Envoy
