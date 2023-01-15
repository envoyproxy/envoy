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
                           public Event::DeferredDeletable {
public:
  IoUringSocketEntry(os_fd_t fd, IoUringWorkerImpl& parent);

  // IoUringSocket
  os_fd_t fd() const override { return fd_; }
  void injectCompletion(RequestType type) override;

  // This will cleanup all the injected completions for this socket and
  // unlink itself from the worker.
  void cleanup();

private:
  void unlink();

  os_fd_t fd_;
  IoUringWorkerImpl& parent_;
};

class IoUringWorkerImpl : public IoUringWorker, protected Logger::Loggable<Logger::Id::io> {
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
  std::unique_ptr<IoUringSocketEntry> removeSocket(IoUringSocketEntry& socket);
  // Inject a request completion into the io_uring instance.
  void injectCompletion(IoUringSocket& socket, RequestType type, int32_t result);
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
  std::list<std::unique_ptr<IoUringSocketEntry>> sockets_;
  // This is used to mark whether the delay submit is enabled.
  // The IoUriingWorks delay the submit the requests which are submitted in request completion
  // callback.
  bool delay_submit_{false};
};

} // namespace Io
} // namespace Envoy
