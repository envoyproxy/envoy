#pragma once

#include "io_uring.h"
#include "source/common/common/logger.h"

#include "source/common/io/io_uring.h"
#include "source/common/io/io_uring_impl.h"

namespace Envoy {
namespace Io {

class IoUringAcceptSocket : public IoUringSocket, protected Logger::Loggable<Logger::Id::io> {
public:
  IoUringAcceptSocket(os_fd_t fd, IoUringImpl& io_uring_impl, IoUringHandler& io_uring_handler, IoUringWorker& parent) :
    fd_(fd), io_uring_impl_(io_uring_impl), io_uring_handler_(io_uring_handler), parent_(parent) {}

  // IoUringSocket
  os_fd_t fd() const override { return fd_; }
  void start() override;
  void close() override;
  void onRequestCompeltion(const Request& req, int32_t result) override;

private:
  void submitRequest();

  os_fd_t fd_;
  IoUringImpl& io_uring_impl_;
  IoUringHandler& io_uring_handler_;
  IoUringWorker& parent_;

  struct sockaddr remote_addr_;
  socklen_t remote_addr_len_{sizeof(remote_addr_)};
  os_fd_t connection_fd_{INVALID_SOCKET};

  Request* accept_req_;
  Request* cancel_req_;
  Request* close_req_;
};


class IoUringWorkerImpl : public IoUringWorker, protected Logger::Loggable<Logger::Id::io> {
public:
  IoUringWorkerImpl(uint32_t io_uring_size, bool use_submission_queue_polling);

  // IoUringWorker
  void start(Event::Dispatcher& dispatcher) override;
  void reset() override { file_event_.reset(); }
  void addAcceptSocket(os_fd_t fd, IoUringHandler& handler) override;
  void closeSocket(os_fd_t fd) override;
  Event::Dispatcher& dispatcher() override;

  std::unique_ptr<IoUringSocket> removeSocket(os_fd_t) override;
  IoUring& get() override;

private:
  void onFileEvent();

  IoUringImpl io_uring_impl_;
  Event::FileEventPtr file_event_{nullptr};
  OptRef<Event::Dispatcher> dispatcher_;

  absl::flat_hash_map<os_fd_t, std::unique_ptr<IoUringSocket>> sockets_;
};

} // namespace Io
} // namespace Envoy