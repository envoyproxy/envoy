#pragma once

#include "source/common/io/io_uring.h"
#include "source/common/io/io_uring_impl.h"

namespace Envoy {
namespace Io {

class IoUringWorkerImpl : public IoUringWorker {
public:
    IoUringWorkerImpl(uint32_t io_uring_size, bool use_submission_queue_polling);
    IoUring& get() override;

private:
    IoUringImpl io_uring_impl_;
};

} // namespace Io
} // namespace Envoy