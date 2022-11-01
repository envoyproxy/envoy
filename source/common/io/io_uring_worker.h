#pragma once

#include "source/common/common/logger.h"

#include "source/common/io/io_uring.h"
#include "source/common/io/io_uring_impl.h"

namespace Envoy {
namespace Io {

// FileEventAdapter adapts `io_uring` to libevent.
class FileEventAdapterImpl : public FileEventAdapter, protected Logger::Loggable<Logger::Id::io> {
public:
  FileEventAdapterImpl(IoUringImpl& io_uring_impl) : io_uring_impl_(io_uring_impl) {}

  void initialize(Event::Dispatcher& dispatcher,
                  Event::FileTriggerType trigger, uint32_t events) override;

  void reset() override { file_event_.reset(); }

private:
  void onFileEvent();

  IoUringImpl& io_uring_impl_;
  Event::FileEventPtr file_event_{nullptr};
};

class IoUringWorkerImpl : public IoUringWorker {
public:
    IoUringWorkerImpl(uint32_t io_uring_size, bool use_submission_queue_polling);
    void initialize(Event::Dispatcher& dispatcher,
                    Event::FileTriggerType trigger, uint32_t events) override;
    void reset() override { file_event_adapter_.reset(); }
    IoUring& get() override;

private:
    IoUringImpl io_uring_impl_;
    FileEventAdapterImpl file_event_adapter_;
};

} // namespace Io
} // namespace Envoy