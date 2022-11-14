#pragma once

#include "io_uring.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"

#include "source/common/io/io_uring.h"
#include "source/common/io/io_uring_impl.h"

namespace Envoy {
namespace Io {

class IoUringAcceptSocket : public IoUringSocket, protected Logger::Loggable<Logger::Id::io> {
public:
  IoUringAcceptSocket(os_fd_t fd, IoUringHandler& io_uring_handler, IoUringWorker& parent) :
    fd_(fd), io_uring_handler_(io_uring_handler), parent_(parent) {}

  // IoUringSocket
  os_fd_t fd() const override { return fd_; }
  void start() override;
  void close() override;
  void enable() override;
  void disable() override;

  void onAccept(int32_t result) override;
  void onClose(int32_t result) override;
  void onCancel(int32_t result) override;
private:
  os_fd_t fd_;

  IoUringHandler& io_uring_handler_;
  IoUringWorker& parent_;

  struct sockaddr remote_addr_;
  socklen_t remote_addr_len_{sizeof(remote_addr_)};
  os_fd_t connection_fd_{INVALID_SOCKET};

  Request* accept_req_;
  Request* cancel_req_;
  Request* close_req_;

  bool is_disabled_{false};
  bool is_pending_accept_{false};
  bool is_closing_{false};
};

class IoUringServerSocket : public IoUringSocket, protected Logger::Loggable<Logger::Id::io> {
public:
  IoUringServerSocket(os_fd_t fd, IoUringHandler& io_uring_handler,
                      IoUringWorker& parent, uint32_t read_buffer_size,
                      bool is_disabled) :
    fd_(fd), io_uring_handler_(io_uring_handler), parent_(parent),
    read_buffer_size_(read_buffer_size), iov_(new struct iovec[1]),
    is_disabled_(is_disabled) {}

  // IoUringSocket
  os_fd_t fd() const override { return fd_; }

  void start() override {
    if (!is_disabled_) {
      submitRequest();
    }
  }

  void onCancel(int32_t result) override {
    cancel_req_ = nullptr;
    ENVOY_LOG(debug, "cancel request done, result = {}, fd = {}", result, fd_);
    if (is_closing_ && read_req_ == nullptr) {
      close_req_ = parent_.submitCloseRequest(*this);
    }
  }

  void onClose(int32_t result) override {
    close_req_ = nullptr;
    ENVOY_LOG(debug, "close request done {}, fd = {}", result, fd_);
    std::unique_ptr<IoUringSocket> self = parent_.removeSocket(fd_);
    parent_.dispatcher().deferredDelete(std::move(self));
  }

  void close() override {
    if (read_req_ != nullptr) {
      is_closing_ = true;
      cancel_req_ = parent_.submitCancelRequest(*this, read_req_);
      return;
    }

    close_req_ = parent_.submitCloseRequest(*this);
  };

  void enable() override {
    ENVOY_LOG(debug, "enable the socket, fd = {}", fd_);
    is_disabled_ = false;
    if (pending_result_ != absl::nullopt) {
      ENVOY_LOG(trace, "inject the pending read result, fd = {}, pending_result_ = {}, pending_buf_size = {}", fd_, pending_result_.value(), pending_read_buf_.length());
      parent_.injectCompletion(*this, RequestType::Read, pending_result_.value());
      pending_result_ = absl::nullopt;
    } else {
      submitRequest();
    }
  };

  void disable() override {
    is_disabled_ = true;
  };

  void onRead(int32_t result) override {
    ENVOY_LOG(trace, "onRead with result {}, fd = {}", result, fd_);
    read_req_ = nullptr;

    if (result <= 0) {
      ASSERT(pending_read_buf_.length() == 0);

      if (result == -ECANCELED) {
        ENVOY_LOG(trace, "read request canceled, fd = {}", fd_);
        if (is_closing_ && cancel_req_ == nullptr) {
          close_req_ = parent_.submitCloseRequest(*this);
        }
        return;
      }

      if (result == -EAGAIN) {
        if (pending_result_.has_value()) {
          result = pending_result_.value();
        }
      }

      if (is_disabled_) {
        ENVOY_LOG(trace, "socket is disabled, pending the nagetive result, fd = {}", fd_);
        pending_result_ = result;
        return;
      }

      ReadParam param{pending_read_buf_, result};
      io_uring_handler_.onRead(param);
      return;
    }

    // If iouring_read_buf_ is nullptr, it means there is pending data.
    if (iouring_read_buf_ != nullptr) {
      Buffer::BufferFragment* fragment = new Buffer::BufferFragmentImpl(
          iouring_read_buf_.release(), result,
          [](const void* data, size_t /*len*/, const Buffer::BufferFragmentImpl* this_fragment) {
            delete[] reinterpret_cast<const uint8_t*>(data);
            delete this_fragment;
          });
      ASSERT(iouring_read_buf_ == nullptr);
      pending_read_buf_.addBufferFragment(*fragment);
    }

    if (is_disabled_) {
      ENVOY_LOG(trace, "socket is disabled, pending the result, fd = {}, pending_buffer size = {}", fd_, pending_read_buf_.length());
      pending_result_ = result;
      return;
    }

    ReadParam param{pending_read_buf_, result};
    io_uring_handler_.onRead(param);
    ASSERT(pending_read_buf_.length() == 0);
    submitRequest();
  }

private:
  void submitRequest() {
    //ASSERT(iouring_read_buf_ == nullptr);
    iouring_read_buf_ = std::make_unique<uint8_t[]>(read_buffer_size_);
    iov_->iov_base = iouring_read_buf_.get();
    iov_->iov_len = read_buffer_size_;
    read_req_ = parent_.submitReadRequest(*this, iov_);
  }

  os_fd_t fd_;

  IoUringHandler& io_uring_handler_;
  IoUringWorker& parent_;

  uint32_t read_buffer_size_;
  std::unique_ptr<uint8_t[]> iouring_read_buf_{};
  Buffer::OwnedImpl pending_read_buf_;
  absl::optional<int32_t> pending_result_{absl::nullopt};
  struct iovec* iov_{nullptr};

  bool is_disabled_{false};
  bool is_closing_{false};

  Request* read_req_;
  Request* cancel_req_;
  Request* close_req_;
};

class IoUringWorkerImpl : public IoUringWorker, protected Logger::Loggable<Logger::Id::io> {
public:
  IoUringWorkerImpl(uint32_t io_uring_size, bool use_submission_queue_polling, Event::Dispatcher& dispatcher);
  ~IoUringWorkerImpl() {
    dispatcher_.clearDeferredDeleteList();
  }

  // IoUringWorker
  void start() override;
  void enableSocket(os_fd_t fd) override {
    auto socket_iter = sockets_.find(fd);
    ASSERT(socket_iter != sockets_.end());
    socket_iter->second->enable();
  }
  void disableSocket(os_fd_t fd) override {
    auto socket_iter = sockets_.find(fd);
    ASSERT(socket_iter != sockets_.end());
    socket_iter->second->disable();
  }
  void addAcceptSocket(os_fd_t fd, IoUringHandler& handler) override;
  void addServerSocket(os_fd_t fd, IoUringHandler& handler, uint32_t read_buffer_size, bool is_disabled) override;
  void closeSocket(os_fd_t fd) override;
  Event::Dispatcher& dispatcher() override;

  std::unique_ptr<IoUringSocket> removeSocket(os_fd_t) override;
  IoUring& get() override;

  Request* submitAcceptRequest(IoUringSocket& socket, struct sockaddr* remote_addr,
                               socklen_t* remote_addr_len) override;
  Request* submitCancelRequest(IoUringSocket& socket, Request* request_to_cancel) override;
  Request* submitCloseRequest(IoUringSocket& socket) override;
  Request* submitReadRequest(IoUringSocket& socket, struct iovec* iov) override;

  void injectCompletion(os_fd_t, RequestType type, int32_t result) override;
  void injectCompletion(IoUringSocket& socket, RequestType type, int32_t result) override;
private:
  void onFileEvent();

  IoUringImpl io_uring_impl_;
  Event::FileEventPtr file_event_{nullptr};
  Event::Dispatcher& dispatcher_;

  absl::flat_hash_map<os_fd_t, std::unique_ptr<IoUringSocket>> sockets_;
};

} // namespace Io
} // namespace Envoy