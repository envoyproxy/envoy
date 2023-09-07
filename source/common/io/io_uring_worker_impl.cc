#include "source/common/io/io_uring_worker_impl.h"

namespace Envoy {
namespace Io {

IoUringSocketEntry::IoUringSocketEntry(os_fd_t fd, IoUringWorkerImpl& parent)
    : fd_(fd), parent_(parent) {}

void IoUringSocketEntry::cleanup() {
  IoUringSocketEntryPtr socket = parent_.removeSocket(*this);
  parent_.dispatcher().deferredDelete(std::move(socket));
}

void IoUringSocketEntry::injectCompletion(Request::RequestType type) {
  // Avoid injecting the same completion type multiple times.
  if (injected_completions_ & static_cast<uint8_t>(type)) {
    ENVOY_LOG(trace,
              "ignore injected completion since there already has one, injected_completions_: {}, "
              "type: {}",
              injected_completions_, static_cast<uint8_t>(type));
    return;
  }
  injected_completions_ |= static_cast<uint8_t>(type);
  parent_.injectCompletion(*this, type, -EAGAIN);
}

IoUringWorkerImpl::IoUringWorkerImpl(uint32_t io_uring_size, bool use_submission_queue_polling,
                                     Event::Dispatcher& dispatcher)
    : IoUringWorkerImpl(std::make_unique<IoUringImpl>(io_uring_size, use_submission_queue_polling),
                        dispatcher) {}

IoUringWorkerImpl::IoUringWorkerImpl(IoUringPtr&& io_uring, Event::Dispatcher& dispatcher)
    : io_uring_(std::move(io_uring)), dispatcher_(dispatcher) {
  const os_fd_t event_fd = io_uring_->registerEventfd();
  // We only care about the read event of Eventfd, since we only receive the
  // event here.
  file_event_ = dispatcher_.createFileEvent(
      event_fd, [this](uint32_t) { onFileEvent(); }, Event::PlatformDefaultTriggerType,
      Event::FileReadyType::Read);
}

IoUringWorkerImpl::~IoUringWorkerImpl() {
  ENVOY_LOG(trace, "destruct io uring worker, existing sockets = {}", sockets_.size());

  dispatcher_.clearDeferredDeleteList();
}

Event::Dispatcher& IoUringWorkerImpl::dispatcher() { return dispatcher_; }

IoUringSocketEntry& IoUringWorkerImpl::addSocket(IoUringSocketEntryPtr&& socket) {
  LinkedList::moveIntoListBack(std::move(socket), sockets_);
  return *sockets_.back();
}

IoUringSocketEntryPtr IoUringWorkerImpl::removeSocket(IoUringSocketEntry& socket) {
  // Remove all the injection completion for this socket.
  io_uring_->removeInjectedCompletion(socket.fd());
  return socket.removeFromList(sockets_);
}

void IoUringWorkerImpl::injectCompletion(IoUringSocket& socket, Request::RequestType type,
                                         int32_t result) {
  Request* req = new Request(type, socket);
  io_uring_->injectCompletion(socket.fd(), req, result);
  file_event_->activate(Event::FileReadyType::Read);
}

void IoUringWorkerImpl::onFileEvent() {
  ENVOY_LOG(trace, "io uring worker, on file event");
  delay_submit_ = true;
  io_uring_->forEveryCompletion([](Request* req, int32_t result, bool injected) {
    ENVOY_LOG(trace, "receive request completion, type = {}, req = {}",
              static_cast<uint8_t>(req->type()), fmt::ptr(req));
    ASSERT(req != nullptr);

    switch (req->type()) {
    case Request::RequestType::Accept:
      ENVOY_LOG(trace, "receive accept request completion, fd = {}, req = {}", req->socket().fd(),
                fmt::ptr(req));
      req->socket().onAccept(req, result, injected);
      break;
    case Request::RequestType::Connect:
      ENVOY_LOG(trace, "receive connect request completion, fd = {}, req = {}", req->socket().fd(),
                fmt::ptr(req));
      req->socket().onConnect(req, result, injected);
      break;
    case Request::RequestType::Read:
      ENVOY_LOG(trace, "receive Read request completion, fd = {}, req = {}", req->socket().fd(),
                fmt::ptr(req));
      req->socket().onRead(req, result, injected);
      break;
    case Request::RequestType::Write:
      ENVOY_LOG(trace, "receive write request completion, fd = {}, req = {}", req->socket().fd(),
                fmt::ptr(req));
      req->socket().onWrite(req, result, injected);
      break;
    case Request::RequestType::Close:
      ENVOY_LOG(trace, "receive close request completion, fd = {}, req = {}", req->socket().fd(),
                fmt::ptr(req));
      req->socket().onClose(req, result, injected);
      break;
    case Request::RequestType::Cancel:
      ENVOY_LOG(trace, "receive cancel request completion, fd = {}, req = {}", req->socket().fd(),
                fmt::ptr(req));
      req->socket().onCancel(req, result, injected);
      break;
    case Request::RequestType::Shutdown:
      ENVOY_LOG(trace, "receive shutdown request completion, fd = {}, req = {}", req->socket().fd(),
                fmt::ptr(req));
      req->socket().onShutdown(req, result, injected);
      break;
    }

    delete req;
  });
  delay_submit_ = false;
  submit();
}

void IoUringWorkerImpl::submit() {
  if (!delay_submit_) {
    io_uring_->submit();
  }
}

} // namespace Io
} // namespace Envoy
