#pragma once

#include "source/common/common/logger.h"

#include "source/common/io/io_uring.h"
#include "source/common/io/io_uring_impl.h"

namespace Envoy {
namespace Io {

class IoUringWorkerImpl : public IoUringWorker, protected Logger::Loggable<Logger::Id::io> {
public:
    IoUringWorkerImpl(uint32_t io_uring_size, bool use_submission_queue_polling);

    void start(Event::Dispatcher& dispatcher) override;
    void reset() override { file_event_.reset(); }

    IoUring& get() override;

private:
    void onFileEvent();

    IoUringImpl io_uring_impl_;
    Event::FileEventPtr file_event_{nullptr};
};

} // namespace Io
} // namespace Envoy