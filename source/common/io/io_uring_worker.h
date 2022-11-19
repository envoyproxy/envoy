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

  sockaddr_storage remote_addr_;
  socklen_t remote_addr_len_{sizeof(remote_addr_)};
  os_fd_t connection_fd_{INVALID_SOCKET};

  Request* accept_req_{nullptr};
  Request* cancel_req_{nullptr};
  Request* close_req_{nullptr};

  bool is_disabled_{false};
  bool is_pending_accept_{false};
  bool is_closing_{false};
};

class IoUringServerSocket : public IoUringSocket, protected Logger::Loggable<Logger::Id::io> {
public:
  IoUringServerSocket(os_fd_t fd, IoUringHandler& io_uring_handler,
                      IoUringWorker& parent, uint32_t read_buffer_size) :
    fd_(fd), io_uring_handler_(io_uring_handler), parent_(parent),
    read_buffer_size_(read_buffer_size), iov_(new struct iovec[1]) {}

  ~IoUringServerSocket() {
    if (SOCKET_VALID(fd_)) {
      ::close(fd_);
    }
  }

  // IoUringSocket
  os_fd_t fd() const override { return fd_; }

  void start() override {
    ENVOY_LOG(debug, "start, fd = {}", fd_);
    if (read_req_ == nullptr) {
      ENVOY_LOG(trace, "submit read request, fd = {}", fd_);
      submitReadRequest();
    }
  }

  void onCancel(int32_t result) override {
    cancel_req_ = nullptr;
    ENVOY_LOG(debug, "cancel request done, result = {}, fd = {}", result, fd_);

    if (is_closing_ && read_req_ == nullptr && write_req_ == nullptr && close_req_ == nullptr) {
      close_req_ = parent_.submitCloseRequest(*this);
    }
  }

  void onClose(int32_t result) override {
    close_req_ = nullptr;
    ENVOY_LOG(debug, "close request done {}, fd = {}", result, fd_);
    ASSERT(read_req_ == nullptr);
    ASSERT(write_req_ == nullptr);
    ASSERT(close_req_ == nullptr);
    ASSERT(cancel_req_ == nullptr);
    std::unique_ptr<IoUringSocket> self = parent_.removeSocket(fd_);
    SET_SOCKET_INVALID(fd_);
    parent_.dispatcher().deferredDelete(std::move(self));
  }

  void close() override {
    is_closing_ = true;
    if (read_req_ != nullptr) {
      ASSERT(cancel_req_ == nullptr);
      cancel_req_ = parent_.submitCancelRequest(*this, read_req_);
      return;
    }
  
    if (write_req_ != nullptr) {
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
      // This could be the case of disable first, but already submit a read request, then enable again, then needn't new request again.
      if (read_req_ == nullptr) {
        ENVOY_LOG(debug, "enable the socket, and submit new read reqest fd = {}", fd_);
        submitReadRequest();
      }
    }
  }

  void disable() override {
    ENVOY_LOG(debug, "disable the socket, fd = {}", fd_);
    is_disabled_ = true;
  };

  void onRead(int32_t result) override {
    ENVOY_LOG(trace, "onRead with result {}, fd = {}, read req = {}", result, fd_, fmt::ptr(read_req_));

    // This is injected event and we have another regular read request, so push this event directly.
    // Or ignored if disabled.
    if (read_req_ != nullptr && result == -EAGAIN) {
      if (!is_disabled_) {
        // TODO: using ptr in read param;
        ENVOY_LOG(trace, "there is a inject event, and same time we have regular read request, fd = {}", fd_);
        Buffer::OwnedImpl temp_buf;
        ReadParam param{temp_buf, result};
        io_uring_handler_.onRead(param);
      }
      return;
    }

    read_req_ = nullptr;

    if (result <= 0) {
      iouring_read_buf_.release();

      if (result == -ECANCELED) {
        ENVOY_LOG(trace, "read request canceled, fd = {}", fd_);
        if (is_closing_ && cancel_req_ == nullptr) {
          close_req_ = parent_.submitCloseRequest(*this);
        }
        return;
      }

      if (is_disabled_) {
        ENVOY_LOG(trace, "socket is disabled, pending the nagetive result, fd = {}, result = {}", fd_, result);
        // ignore injected event when disabled.
        if (result != -EAGAIN) {
          pending_result_ = result;
        }
        return;
      }

      if (result == -EAGAIN && is_closing_) {
        ENVOY_LOG(trace, "ignore injected event when closing");
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
      ENVOY_LOG(debug, "has pending data, fd = {}", fd_);
      ASSERT(iouring_read_buf_ == nullptr);
      pending_read_buf_.addBufferFragment(*fragment);
    }

    if (is_disabled_) {
      ENVOY_LOG(trace, "socket is disabled, pending the result, fd = {}, pending_buffer size = {}", fd_, pending_read_buf_.length());
      pending_result_ = result;
      return;
    }

    ReadParam param{pending_read_buf_, result};
    ENVOY_LOG(trace, "calling onRead callback, fd = {}, pending read buf size = {}, result = {}", fd_, pending_read_buf_.length(), result);
    io_uring_handler_.onRead(param);
    ASSERT(pending_read_buf_.length() == 0);
    if (read_req_ == nullptr && !is_closing_) {
      ENVOY_LOG(trace, "submit read request after previous read complete, fd = {}", fd_);
      submitReadRequest();
    }
  }

  void onWrite(int32_t result, Request* req) override {
    ENVOY_LOG(trace, "onWrite with result {}, fd = {}", result, fd_);
    bool injected_event = (result == -EAGAIN && write_req_ != req);
    if (injected_event) {
      if (is_closing_) {
        ENVOY_LOG(trace, "there is a inject event, but is closing, ignore it, fd = {}", fd_);
        return;
      }
       // TODO: using ptr in read param;
      ENVOY_LOG(trace, "there is a inject event, and same time we have regular write request, fd = {}", fd_);
      WriteParam param{result};
      io_uring_handler_.onWrite(param);
      return;
    }

    write_req_ = nullptr;
    if (iovecs_ != nullptr) {
      delete iovecs_;
      iovecs_ = nullptr;
    }

    if (result <= 0) {
      if (is_closing_ && read_req_ == nullptr && cancel_req_ == nullptr && write_req_ == nullptr && close_req_ == nullptr) {
        ASSERT(close_req_ == nullptr);
        close_req_ = parent_.submitCloseRequest(*this);
        return;
      }

      if (result == -EAGAIN) {
        if (is_closing_) {
          ENVOY_LOG(trace, "ignore injected event when closing");
          return;
        }
        if (!injected_event) {
          if (write_buf_.length() > 0) {
            ENVOY_LOG(trace, "continue write buf since get eagain, size = {}, fd = {}", write_buf_.length(), fd_);
            submitWriteRequest();
          }
          return;
        }
      }

      WriteParam param{result};
      io_uring_handler_.onWrite(param);
      return;
    }

    write_buf_.drain(result);
    ENVOY_LOG(trace, "drain write buf, drain size = {}, fd = {}", result, fd_);
    if (write_buf_.length() > 0) {
      ENVOY_LOG(trace, "continue write buf since still have data, size = {}, fd = {}", write_buf_.length(), fd_);
      submitWriteRequest();
      return;
    }

    if (is_closing_ && read_req_ == nullptr && cancel_req_ == nullptr) {
      ENVOY_LOG(trace, "write is done, try to close the socket, fd = {}", fd_);
      ASSERT(close_req_ == nullptr);
      close_req_ = parent_.submitCloseRequest(*this);
      return;
    }
  
    if (is_full_ && write_buf_.length() == 0) {
      is_full_ = false;
      WriteParam param{result};
      io_uring_handler_.onWrite(param);
    }
  }

  uint64_t write(Buffer::Instance& buffer) override {
    ENVOY_LOG(trace, "write, size = {}, fd = {}, slices = {}", buffer.length(), fd_, buffer.getRawSlices().size());
    uint64_t buffer_limit = 1024 * 1024;
    uint64_t writable_size = buffer_limit - write_buf_.length();
    if (writable_size <= 0) {
      is_full_ = true;
      return 0;
    }
    auto length_to_write = std::min(buffer.length(), writable_size);
    write_buf_.move(buffer, length_to_write);

    if (write_req_ != nullptr) {
      ENVOY_LOG(trace, "write already submited, fd = {}", fd_);
      return length_to_write;
    }

    ENVOY_LOG(trace, "write buf, size = {}, slices = {}", write_buf_.length(), write_buf_.getRawSlices().size());
    submitWriteRequest();
    return length_to_write;
  }

  uint64_t writev(const Buffer::RawSlice* slices, uint64_t num_slice) override {
    ENVOY_LOG(trace, "writev, num_slices = {}, fd = {}", num_slice, fd_);

    uint64_t data_to_write = 0;
    for (uint64_t i = 0; i < num_slice; i++) {
      write_buf_.add(slices[i].mem_, slices[i].len_);
      data_to_write += slices[i].len_;
    }

    if (write_req_ != nullptr) {
      ENVOY_LOG(trace, "write already submited, fd = {}", fd_);
      return data_to_write;
    }

    submitWriteRequest();
    return data_to_write;
  }

private:
  void submitReadRequest() {
    ENVOY_LOG(trace, "submit read request, fd = {}", fd_);
    ASSERT(read_req_ == nullptr);
    ASSERT(iouring_read_buf_ == nullptr);
    iouring_read_buf_ = std::make_unique<uint8_t[]>(read_buffer_size_);
    iov_->iov_base = iouring_read_buf_.get();
    iov_->iov_len = read_buffer_size_;
    read_req_ = parent_.submitReadRequest(*this, iov_);
  }

  void submitWriteRequest() {
    ASSERT(write_req_ == nullptr);
    ASSERT(iovecs_ == nullptr);

    Buffer::RawSliceVector slices = write_buf_.getRawSlices();
    iovecs_ = new struct iovec[slices.size()];
    for (uint64_t i = 0; i < slices.size(); i++) {
      iovecs_[i].iov_base = slices[i].mem_;
      iovecs_[i].iov_len = slices[i].len_;
    }

    ENVOY_LOG(trace, "submit write request, write_buf size = {}, num_iovecs = {}, fd = {}", write_buf_.length(), slices.size(), fd_);

    write_req_ = parent_.submitWritevRequest(*this, iovecs_, slices.size());
  }

  os_fd_t fd_;

  IoUringHandler& io_uring_handler_;
  IoUringWorker& parent_;

  // for read
  uint32_t read_buffer_size_;
  std::unique_ptr<uint8_t[]> iouring_read_buf_{nullptr};
  Buffer::OwnedImpl pending_read_buf_;
  absl::optional<int32_t> pending_result_{absl::nullopt};
  struct iovec* iov_{nullptr};

  // for write
  Buffer::OwnedImpl write_buf_;
  struct iovec* iovecs_{nullptr};

  bool is_disabled_{false};
  bool is_closing_{false};

  Request* read_req_{nullptr};
  Request* cancel_req_{nullptr};
  Request* close_req_{nullptr};
  Request* write_req_{nullptr};

  //TODO Using water maker
  bool is_full_{false};
};

class IoUringWorkerImpl : public IoUringWorker, protected Logger::Loggable<Logger::Id::io> {
public:
  IoUringWorkerImpl(uint32_t io_uring_size, bool use_submission_queue_polling, Event::Dispatcher& dispatcher);
  ~IoUringWorkerImpl() {
    ENVOY_LOG(trace, "destruct io uring worker");
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
  void addServerSocket(os_fd_t fd, IoUringHandler& handler, uint32_t read_buffer_size) override;
  void closeSocket(os_fd_t fd) override;
  Event::Dispatcher& dispatcher() override;

  IoUringSocket& getIoUringSocket(os_fd_t fd) override;

  std::unique_ptr<IoUringSocket> removeSocket(os_fd_t) override;
  IoUring& get() override;

  Request* submitAcceptRequest(IoUringSocket& socket, sockaddr_storage* remote_addr,
                               socklen_t* remote_addr_len) override;
  Request* submitCancelRequest(IoUringSocket& socket, Request* request_to_cancel) override;
  Request* submitCloseRequest(IoUringSocket& socket) override;
  Request* submitReadRequest(IoUringSocket& socket, struct iovec* iov) override;
  Request* submitWritevRequest(IoUringSocket& socket, struct iovec* iovecs, uint64_t num_vecs) override;

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