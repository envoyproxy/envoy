#include "source/common/io/io_uring_worker.h"

namespace Envoy {
namespace Io {

void IoUringWorkerImpl::onFileEvent() {
  io_uring_impl_.forEveryCompletion([](void* user_data, int32_t result) {
    auto req = static_cast<Io::Request*>(user_data);

    if (result < 0) {
      ENVOY_LOG(debug, "async request failed: {}", errorDetails(-result));
    }

    // For close, there is no iohandle value, but need to fix
    if (!req->io_uring_handler_.has_value()) {
      ENVOY_LOG(debug, "no iohandle");
      return;
    }
    req->io_uring_handler_->get().onRequestCompletion(*req, result);

    delete req;
  });
  io_uring_impl_.submit();
}

IoUringWorkerImpl::IoUringWorkerImpl(uint32_t io_uring_size, bool use_submission_queue_polling) :
    io_uring_impl_(io_uring_size, use_submission_queue_polling) { }

void IoUringWorkerImpl::start(Event::Dispatcher& dispatcher) {
  // This means already registered the file event.
  if (file_event_ != nullptr) {
    return;
  }

  const os_fd_t event_fd = io_uring_impl_.registerEventfd();
  // We only care about the read event of Eventfd, since we only receive the
  // event here.
  file_event_ = dispatcher.createFileEvent(
      event_fd, [this](uint32_t) { onFileEvent(); }, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
}

IoUring& IoUringWorkerImpl::get() {
    return io_uring_impl_;
}

} // namespace Io
} // namespace Envoy