#pragma once

#include "io_uring.h"
#include "source/common/common/logger.h"

#include "source/common/io/io_uring.h"
#include "source/common/io/io_uring_impl.h"

namespace Envoy {
namespace Io {

class IoUringAcceptSocket : public IoUringSocket, protected Logger::Loggable<Logger::Id::io>  {
public:
  IoUringAcceptSocket(os_fd_t fd, IoUringImpl& io_uring_impl, IoUringHandler& io_uring_handler) : fd_(fd), io_uring_impl_(io_uring_impl), io_uring_handler_(io_uring_handler) {}

  os_fd_t fd() const override {
    return fd_;
  }

  void start() override {
    submitRequest();
  }

  void onRequestCompeltion(const Request& req, int32_t result) override {
    if (req.type_ == RequestType::Accept) {
      if (result < 0) {
        ENVOY_LOG(debug, "Accept request failed");
        return;
      }
      ENVOY_LOG(debug, "New socket accepted");
      connection_fd_ = result;
      AcceptedSocketParam param{connection_fd_, remote_addr_, remote_addr_len_};
      io_uring_handler_.onAcceptSocket(param);
      submitRequest();
      return;
    }
    ASSERT(false);
  }

private:
  void submitRequest() {
    auto req = new Request();
    req->type_ = RequestType::Accept;
    req->io_uring_socket_ = *this;
  
    auto res = io_uring_impl_.prepareAccept(fd_, &remote_addr_, &remote_addr_len_, req);
    if (res == Io::IoUringResult::Failed) {
      // TODO(rojkov): handle `EBUSY` in case the completion queue is never reaped.
      io_uring_impl_.submit();
      res = io_uring_impl_.prepareAccept(fd_, &remote_addr_, &remote_addr_len_, req);
      RELEASE_ASSERT(res == Io::IoUringResult::Ok, "unable to prepare accept");
    }
    ENVOY_LOG(debug, "Submit new accept request");
    io_uring_impl_.submit();
  }

  os_fd_t fd_;
  IoUringImpl& io_uring_impl_;
  IoUringHandler& io_uring_handler_;

  struct sockaddr remote_addr_;
  socklen_t remote_addr_len_{sizeof(remote_addr_)};
  os_fd_t connection_fd_{INVALID_SOCKET};
};


class IoUringWorkerImpl : public IoUringWorker, protected Logger::Loggable<Logger::Id::io> {
public:
  IoUringWorkerImpl(uint32_t io_uring_size, bool use_submission_queue_polling);

  void start(Event::Dispatcher& dispatcher) override;
  void reset() override { file_event_.reset(); }

  void addAcceptSocket(os_fd_t fd, IoUringHandler& handler) override {
    std::unique_ptr<IoUringAcceptSocket> socket = std::make_unique<IoUringAcceptSocket>(fd, io_uring_impl_, handler);
    socket->start();
    sockets_.insert({fd, std::move(socket)});
    io_uring_impl_.submit();
  }

  IoUring& get() override;

private:
  void onFileEvent();

  IoUringImpl io_uring_impl_;
  Event::FileEventPtr file_event_{nullptr};

  absl::flat_hash_map<os_fd_t, std::unique_ptr<IoUringSocket>> sockets_;
};

} // namespace Io
} // namespace Envoy