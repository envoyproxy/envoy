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

class BaseRequest : public Request {
public:
  BaseRequest(uint32_t type, IoUringSocket& socket);

  // Request
  uint32_t type() const override { return type_; }
  IoUringSocket& socket() const override { return socket_; }

  uint32_t type_;
  IoUringSocket& socket_;
};

class AcceptRequest : public BaseRequest {
public:
  AcceptRequest(IoUringSocket& socket);

  size_t i_{};
  sockaddr_storage remote_addr_{};
  socklen_t remote_addr_len_{sizeof(remote_addr_)};
};

class ReadRequest : public BaseRequest {
public:
  ReadRequest(IoUringSocket& socket, uint32_t size);

  std::unique_ptr<uint8_t[]> buf_;
  std::unique_ptr<struct iovec> iov_;
};
class WriteRequest : public BaseRequest {
public:
  WriteRequest(IoUringSocket& socket, const Buffer::RawSliceVector& slices);

  std::unique_ptr<struct iovec[]> iov_;
};

class IoUringSocketEntry : public IoUringSocket,
                           public LinkedObject<IoUringSocketEntry>,
                           public Event::DeferredDeletable,
                           protected Logger::Loggable<Logger::Id::io> {
public:
  IoUringSocketEntry(os_fd_t fd, IoUringWorkerImpl& parent, IoUringHandler& io_uring_handler);

  // IoUringSocket
  os_fd_t fd() const override { return fd_; }
  void close() override { status_ = CLOSING; }
  void enable() override { status_ = ENABLED; }
  void disable() override { status_ = DISABLED; }
  void connect(const Network::Address::InstanceConstSharedPtr&) override { PANIC("not implement"); }
  void write(Buffer::Instance&) override { PANIC("not implement"); }
  uint64_t write(const Buffer::RawSlice*, uint64_t) override { PANIC("not implement"); }
  void shutdown(int) override { PANIC("not implement"); }
  // This will cleanup all the injected completions for this socket and
  // unlink itself from the worker.
  void cleanup();
  void onAccept(Request*, int32_t, bool injected) override {
    if (injected && (injected_completions_ & RequestType::Accept)) {
      injected_completions_ &= ~RequestType::Accept;
    }
  }
  void onConnect(Request*, int32_t, bool injected) override {
    if (injected && (injected_completions_ & RequestType::Connect)) {
      injected_completions_ &= ~RequestType::Connect;
    }
  }
  void onRead(Request*, int32_t, bool injected) override {
    if (injected && (injected_completions_ & RequestType::Read)) {
      injected_completions_ &= ~RequestType::Read;
    }
  }
  void onWrite(Request*, int32_t, bool injected) override {
    if (injected && (injected_completions_ & RequestType::Write)) {
      injected_completions_ &= ~RequestType::Write;
    }
  }
  void onClose(Request*, int32_t, bool injected) override {
    if (injected && (injected_completions_ & RequestType::Close)) {
      injected_completions_ &= ~RequestType::Close;
    }
  }
  void onCancel(Request*, int32_t, bool injected) override {
    if (injected && (injected_completions_ & RequestType::Cancel)) {
      injected_completions_ &= ~RequestType::Cancel;
    }
  }
  void onShutdown(Request*, int32_t, bool injected) override {
    if (injected && (injected_completions_ & RequestType::Shutdown)) {
      injected_completions_ &= ~RequestType::Shutdown;
    }
  }
  void injectCompletion(uint32_t type) override;
  IoUringSocketStatus getStatus() const override { return status_; }

protected:
  os_fd_t fd_;
  IoUringWorkerImpl& parent_;
  IoUringHandler& io_uring_handler_;
  uint32_t injected_completions_{0};
  IoUringSocketStatus status_{INITIALIZED};
};

using IoUringSocketEntryPtr = std::unique_ptr<IoUringSocketEntry>;

class IoUringWorkerImpl : public IoUringWorker, private Logger::Loggable<Logger::Id::io> {
public:
  IoUringWorkerImpl(uint32_t io_uring_size, bool use_submission_queue_polling, uint32_t accept_size,
                    uint32_t read_buffer_size, uint32_t write_timeout_ms,
                    Event::Dispatcher& dispatcher);
  IoUringWorkerImpl(IoUringPtr io_uring, uint32_t accept_size, uint32_t read_buffer_size,
                    uint32_t write_timeout_ms, Event::Dispatcher& dispatcher);
  ~IoUringWorkerImpl() override;

  // IoUringWorker
  IoUringSocket& addAcceptSocket(os_fd_t fd, IoUringHandler& handler) override;
  IoUringSocket& addServerSocket(os_fd_t fd, IoUringHandler& handler) override;
  IoUringSocket& addClientSocket(os_fd_t fd, IoUringHandler& handler) override;

  Event::Dispatcher& dispatcher() override;

  Request* submitAcceptRequest(IoUringSocket& socket) override;
  Request* submitConnectRequest(IoUringSocket& socket,
                                const Network::Address::InstanceConstSharedPtr& address) override;
  Request* submitReadRequest(IoUringSocket& socket) override;
  Request* submitWriteRequest(IoUringSocket& socket, const Buffer::RawSliceVector& slices) override;
  Request* submitCloseRequest(IoUringSocket& socket) override;
  Request* submitCancelRequest(IoUringSocket& socket, Request* request_to_cancel) override;
  Request* submitShutdownRequest(IoUringSocket& socket, int how) override;

  // From socket from the worker.
  IoUringSocketEntryPtr removeSocket(IoUringSocketEntry& socket);
  // Inject a request completion into the io_uring instance.
  void injectCompletion(IoUringSocket& socket, uint32_t type, int32_t result);
  // Remove all the injected completion for the specific socket.
  void removeInjectedCompletion(IoUringSocket& socket);

protected:
  // The io_uring instance.
  IoUringPtr io_uring_;
  const uint32_t accept_size_;
  const uint32_t read_buffer_size_;
  const uint32_t write_timeout_ms_;
  Event::Dispatcher& dispatcher_;
  // The file event of io_uring's eventfd.
  Event::FileEventPtr file_event_{nullptr};
  // All the sockets in this worker.
  std::list<IoUringSocketEntryPtr> sockets_;
  // This is used to mark whether delay submit is enabled.
  // The IoUriingWorks delay the submit the requests which are submitted in request completion
  // callback.
  bool delay_submit_{false};

  void onFileEvent();
  void submit();
};

class IoUringAcceptSocket : public IoUringSocketEntry {
public:
  IoUringAcceptSocket(os_fd_t fd, IoUringWorkerImpl& parent, IoUringHandler& io_uring_handler,
                      uint32_t accept_size);

  void close() override;
  void enable() override;
  void disable() override;
  void onClose(Request* req, int32_t result, bool injected) override;
  void onAccept(Request* req, int32_t result, bool injected) override;

private:
  const uint32_t accept_size_;
  // These are used to track the current submitted accept requests.
  size_t request_count_{};
  std::vector<Request*> requests_;

  void submitRequests();
};

class IoUringServerSocket : public IoUringSocketEntry {
public:
  IoUringServerSocket(os_fd_t fd, IoUringWorkerImpl& parent, IoUringHandler& io_uring_handler,
                      uint32_t write_timeout_ms);

  void close() override;
  void enable() override;
  void disable() override;
  void write(Buffer::Instance& data) override;
  uint64_t write(const Buffer::RawSlice* slices, uint64_t num_slice) override;
  void shutdown(int how) override;
  void onClose(Request* req, int32_t result, bool injected) override;
  void onRead(Request* req, int32_t result, bool injected) override;
  void onWrite(Request* req, int32_t result, bool injected) override;
  void onCancel(Request* req, int32_t, bool injected) override;
  void onShutdown(Request* req, int32_t, bool injected) override;

private:
  const uint32_t write_timeout_ms_;
  // For read.
  Request* read_req_{};
  // TODO (soulxu): Add water mark here.
  Buffer::OwnedImpl buf_;
  // TODO (soulxu): using queue for completion.
  absl::optional<int32_t> read_error_;

  // For write.
  Buffer::OwnedImpl write_buf_;
  struct iovec* iovecs_{nullptr};
  Request* write_req_{nullptr};
  Event::TimerPtr write_timeout_timer_{nullptr};

  // This is used for avoid duplicated close or cancel.
  Request* close_req_{nullptr};
  Request* cancel_read_req_{nullptr};
  Request* cancel_write_req_{nullptr};

  void submitReadRequest();
  void submitWriteRequest();
};

} // namespace Io
} // namespace Envoy
