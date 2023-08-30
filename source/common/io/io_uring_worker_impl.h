#pragma once

#include "envoy/common/io/io_uring.h"

#include "source/common/common/linked_object.h"
#include "source/common/io/io_uring_impl.h"

namespace Envoy {
namespace Io {

class IoUringSocketEntry;
using IoUringSocketEntryPtr = std::unique_ptr<IoUringSocketEntry>;

class IoUringWorkerImpl : public IoUringWorker, private Logger::Loggable<Logger::Id::io> {
public:
  IoUringWorkerImpl(uint32_t io_uring_size, bool use_submission_queue_polling,
                    Event::Dispatcher& dispatcher);
  IoUringWorkerImpl(IoUringPtr&& io_uring, Event::Dispatcher& dispatcher);
  ~IoUringWorkerImpl() override;

  // IoUringWorker
  Event::Dispatcher& dispatcher() override;

  // Remove a socket from this worker.
  IoUringSocketEntryPtr removeSocket(IoUringSocketEntry& socket);

  // Inject a request completion into the iouring instance for a specific socket.
  void injectCompletion(IoUringSocket& socket, Request::RequestType type, int32_t result);

  // Return the number of sockets in this worker.
  size_t getNumOfSockets() const { return sockets_.size(); }

protected:
  // Add a socket to the worker.
  IoUringSocketEntry& addSocket(IoUringSocketEntryPtr&& socket);
  void onFileEvent();
  void submit();

  // The iouring instance.
  IoUringPtr io_uring_;
  // The dispatcher of this worker is running on.
  Event::Dispatcher& dispatcher_;
  // The file event of iouring's eventfd.
  Event::FileEventPtr file_event_{nullptr};
  // All the sockets in this worker.
  std::list<IoUringSocketEntryPtr> sockets_;
  // This is used to mark whether delay submit is enabled.
  // The IoUringWorker will delay the submit the requests which are submitted in request completion
  // callback.
  bool delay_submit_{false};
};

class IoUringSocketEntry : public IoUringSocket,
                           public LinkedObject<IoUringSocketEntry>,
                           public Event::DeferredDeletable,
                           protected Logger::Loggable<Logger::Id::io> {
public:
  IoUringSocketEntry(os_fd_t fd, IoUringWorkerImpl& parent);

  // IoUringSocket
  IoUringWorker& getIoUringWorker() const override { return parent_; }
  os_fd_t fd() const override { return fd_; }
  void onAccept(Request*, int32_t, bool injected) override {
    if (injected && (injected_completions_ & static_cast<uint8_t>(Request::RequestType::Accept))) {
      injected_completions_ &= ~static_cast<uint8_t>(Request::RequestType::Accept);
    }
  }
  void onConnect(Request*, int32_t, bool injected) override {
    if (injected && (injected_completions_ & static_cast<uint8_t>(Request::RequestType::Connect))) {
      injected_completions_ &= ~static_cast<uint8_t>(Request::RequestType::Connect);
    }
  }
  void onRead(Request*, int32_t, bool injected) override {
    if (injected && (injected_completions_ & static_cast<uint8_t>(Request::RequestType::Read))) {
      injected_completions_ &= ~static_cast<uint8_t>(Request::RequestType::Read);
    }
  }
  void onWrite(Request*, int32_t, bool injected) override {
    if (injected && (injected_completions_ & static_cast<uint8_t>(Request::RequestType::Write))) {
      injected_completions_ &= ~static_cast<uint8_t>(Request::RequestType::Write);
    }
  }
  void onClose(Request*, int32_t, bool injected) override {
    if (injected && (injected_completions_ & static_cast<uint8_t>(Request::RequestType::Close))) {
      injected_completions_ &= ~static_cast<uint8_t>(Request::RequestType::Close);
    }
  }
  void onCancel(Request*, int32_t, bool injected) override {
    if (injected && (injected_completions_ & static_cast<uint8_t>(Request::RequestType::Cancel))) {
      injected_completions_ &= ~static_cast<uint8_t>(Request::RequestType::Cancel);
    }
  }
  void onShutdown(Request*, int32_t, bool injected) override {
    if (injected &&
        (injected_completions_ & static_cast<uint8_t>(Request::RequestType::Shutdown))) {
      injected_completions_ &= ~static_cast<uint8_t>(Request::RequestType::Shutdown);
    }
  }
  void injectCompletion(Request::RequestType type) override;

protected:
  /**
   * For the socket to remove itself from the IoUringWorker and defer deletion.
   */
  void cleanup();

  os_fd_t fd_{INVALID_SOCKET};
  IoUringWorkerImpl& parent_;
  // This records already injected completion request type to
  // avoid duplicated injections.
  uint8_t injected_completions_{0};
};

} // namespace Io
} // namespace Envoy
