#pragma once

#include "envoy/common/io/io_uring.h"
#include "envoy/event/file_event.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/linked_object.h"
#include "source/common/common/logger.h"
#include "source/common/io/io_uring_impl.h"

namespace Envoy {
namespace Io {

class IoUringWorkerImpl;

class IoUringSocketEntry : public IoUringSocket,
                           public LinkedObject<IoUringSocketEntry>,
                           public Event::DeferredDeletable,
                           protected Logger::Loggable<Logger::Id::io> {
public:
  IoUringSocketEntry(os_fd_t fd, IoUringWorkerImpl& parent);

  // IoUringSocket
  os_fd_t fd() const override { return fd_; }
  void injectCompletion(uint32_t type) override;

  // This will cleanup all the injected completions for this socket and
  // unlink itself from the worker.
  void cleanup();
  void onAccept(int32_t, bool injected) override {
    if (injected && (injected_completions_ & RequestType::Accept)) {
      injected_completions_ &= ~RequestType::Accept;
    }
  }
  void onClose(int32_t, bool injected) override {
    if (injected && (injected_completions_ & RequestType::Close)) {
      injected_completions_ &= ~RequestType::Close;
    }
  }
  void onCancel(int32_t, bool injected) override {
    if (injected && (injected_completions_ & RequestType::Cancel)) {
      injected_completions_ &= ~RequestType::Cancel;
    }
  }
  void onConnect(int32_t, bool injected) override {
    if (injected && (injected_completions_ & RequestType::Connect)) {
      injected_completions_ &= ~RequestType::Connect;
    }
  }
  void onRead(int32_t, bool injected) override {
    if (injected && (injected_completions_ & RequestType::Read)) {
      injected_completions_ &= ~RequestType::Read;
    }
  }
  void onWrite(int32_t, bool injected) override {
    if (injected && (injected_completions_ & RequestType::Write)) {
      injected_completions_ &= ~RequestType::Write;
    }
  }

private:
  os_fd_t fd_;
  IoUringWorkerImpl& parent_;
  uint32_t injected_completions_{0};
};

using IoUringSocketEntryPtr = std::unique_ptr<IoUringSocketEntry>;

class IoUringWorkerImpl : public IoUringWorker, private Logger::Loggable<Logger::Id::io> {
public:
  IoUringWorkerImpl(uint32_t io_uring_size, bool use_submission_queue_polling,
                    Event::Dispatcher& dispatcher);
  IoUringWorkerImpl(std::unique_ptr<IoUring> io_uring_instance, Event::Dispatcher& dispatcher);
  ~IoUringWorkerImpl() override;

  // IoUringWorker
  IoUringSocket& addAcceptSocket(os_fd_t fd, IoUringHandler& handler) override;
  IoUringSocket& addServerSocket(os_fd_t fd, IoUringHandler& handler,
                                 uint32_t read_buffer_size) override;
  IoUringSocket& addClientSocket(os_fd_t fd, IoUringHandler& handler,
                                 uint32_t read_buffer_size) override;

  Event::Dispatcher& dispatcher() override;

  Request* submitAcceptRequest(IoUringSocket& socket, sockaddr_storage* remote_addr,
                               socklen_t* remote_addr_len) override;
  Request* submitCancelRequest(IoUringSocket& socket, Request* request_to_cancel) override;
  Request* submitCloseRequest(IoUringSocket& socket) override;
  Request* submitReadRequest(IoUringSocket& socket, struct iovec* iov) override;
  Request* submitWritevRequest(IoUringSocket& socket, struct iovec* iovecs,
                               uint64_t num_vecs) override;
  Request* submitConnectRequest(IoUringSocket& socket,
                                const Network::Address::InstanceConstSharedPtr& address) override;

  // From socket from the worker.
  IoUringSocketEntryPtr removeSocket(IoUringSocketEntry& socket);
  // Inject a request completion into the io_uring instance.
  void injectCompletion(IoUringSocket& socket, uint32_t type, int32_t result);
  // Remove all the injected completion for the specific socket.
  void removeInjectedCompletion(IoUringSocket& socket);

protected:
  void onFileEvent();
  void submit();

  // The io_uring instance.
  std::unique_ptr<IoUring> io_uring_instance_;
  // The file event of io_uring's eventfd.
  Event::FileEventPtr file_event_{nullptr};
  Event::Dispatcher& dispatcher_;
  // All the sockets in this worker.
  std::list<IoUringSocketEntryPtr> sockets_;
  // This is used to mark whether delay submit is enabled.
  // The IoUriingWorks delay the submit the requests which are submitted in request completion
  // callback.
  bool delay_submit_{false};
};

} // namespace Io
} // namespace Envoy
