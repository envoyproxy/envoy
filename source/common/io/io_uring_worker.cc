#include "source/common/io/io_uring_worker.h"

namespace Envoy {
namespace Io {

IoUringWorkerImpl::IoUringWorkerImpl(uint32_t io_uring_size, bool use_submission_queue_polling) :
    io_uring_impl_(io_uring_size, use_submission_queue_polling) { }

IoUring& IoUringWorkerImpl::get() {
    return io_uring_impl_;
}

} // namespace Io
} // namespace Envoy