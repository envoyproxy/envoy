#include "source/common/io/io_uring_worker.h"
#include "io_uring.h"

namespace Envoy {
namespace Io {

void IoUringWorkerImpl::onFileEvent() {
  io_uring_impl_.forEveryCompletion([](void* user_data, int32_t result) {
    auto req = static_cast<Io::Request*>(user_data);

    if (result < 0) {
      ENVOY_LOG(debug, "async request failed: {}", errorDetails(-result));
    }

    // temp log and temp fix for the old path, remove then when I fix the thing.
    switch(req->type_) {
      case RequestType::Accept:
        ENVOY_LOG(debug, "receive accept request completion");
        break;
      case RequestType::Connect:
        ENVOY_LOG(debug, "receive connect request completion");
        break;
      case RequestType::Read:
        ENVOY_LOG(debug, "receive Read request completion");
        break;
      case RequestType::Write:
        ENVOY_LOG(debug, "receive write request completion");
        break;
      case RequestType::Close:
        ENVOY_LOG(debug, "receive close request completion");
        break;
      case RequestType::Cancel:
        ENVOY_LOG(debug, "receive cancel request completion");
        // Return is temp fix.
        if (!req->io_uring_socket_.has_value()) {
          return;
        }
        break;
      case RequestType::Unknown:
        ENVOY_LOG(debug, "receive unknown request completion");
        break;
    }
    // temp fix for the old path
    if (!req->io_uring_socket_.has_value() && result == -ECANCELED) {
      ENVOY_LOG(debug, "the request is cancel, then return directly.");
      return;
    }
  
    if (req->io_uring_socket_.has_value()) {
      req->io_uring_socket_->get().onRequestCompeltion(*req, result);
    // For close, there is no iohandle value, but need to fix
    } else if (req->io_uring_handler_.has_value()) {
      req->io_uring_handler_->get().onRequestCompletion(*req, result);
    } else {
      ENVOY_LOG(debug, "no iohandle");
      return;
    }

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