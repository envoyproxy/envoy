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

  std::unique_ptr<IoUringSocketEntry> unlink();

private:
  os_fd_t fd_;
  IoUringWorkerImpl& parent_;
};

class IoUringWorkerImpl : public IoUringWorker, protected Logger::Loggable<Logger::Id::io> {
public:
  IoUringWorkerImpl(uint32_t io_uring_size, bool use_submission_queue_polling,
                    Event::Dispatcher& dispatcher);
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

  std::unique_ptr<IoUringSocketEntry> removeSocket(IoUringSocketEntry& socket);

protected:
  void onFileEvent();
  void submit();

  IoUringImpl io_uring_impl_;
  Event::FileEventPtr file_event_{nullptr};
  Event::Dispatcher& dispatcher_;

  std::list<std::unique_ptr<IoUringSocketEntry>> sockets_;
  bool delay_submit_{false};
};

} // namespace Io
} // namespace Envoy
