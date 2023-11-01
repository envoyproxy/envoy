#pragma once

#include <atomic>

#include "envoy/common/io/io_uring.h"
#include "envoy/event/file_event.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/linked_object.h"
#include "source/common/common/logger.h"
#include "source/common/io/io_uring_impl.h"

namespace Envoy {
namespace Io {

class AcceptRequest : public Request {
public:
  AcceptRequest(IoUringSocket& socket);

  size_t i_{};
  sockaddr_storage remote_addr_{};
  socklen_t remote_addr_len_{sizeof(remote_addr_)};
};

class ReadRequest : public Request {
public:
  ReadRequest(IoUringSocket& socket, uint32_t size);

  std::unique_ptr<uint8_t[]> buf_;
  std::unique_ptr<struct iovec> iov_;
};

class WriteRequest : public Request {
public:
  WriteRequest(IoUringSocket& socket, const Buffer::RawSliceVector& slices);

  std::unique_ptr<struct iovec[]> iov_;
};

class IoUringSocketEntry;
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
  IoUringSocket& addAcceptSocket(os_fd_t fd, Event::FileReadyCb cb,
                                 bool enable_close_event) override;
  IoUringSocket& addServerSocket(os_fd_t fd, Event::FileReadyCb cb,
                                 bool enable_close_event) override;
  IoUringSocket& addServerSocket(os_fd_t fd, Buffer::Instance& read_buf, Event::FileReadyCb cb,
                                 bool enable_close_event) override;
  IoUringSocket& addClientSocket(os_fd_t fd, Event::FileReadyCb cb,
                                 bool enable_close_event) override;

  Request* submitAcceptRequest(IoUringSocket& socket) override;
  Request* submitConnectRequest(IoUringSocket& socket,
                                const Network::Address::InstanceConstSharedPtr& address) override;
  Request* submitReadRequest(IoUringSocket& socket) override;
  Request* submitWriteRequest(IoUringSocket& socket, const Buffer::RawSliceVector& slices) override;
  Request* submitCloseRequest(IoUringSocket& socket) override;
  Request* submitCancelRequest(IoUringSocket& socket, Request* request_to_cancel) override;
  Request* submitShutdownRequest(IoUringSocket& socket, int how) override;
  // IoUringWorker
  Event::Dispatcher& dispatcher() override;

  // Remove a socket from this worker.
  IoUringSocketEntryPtr removeSocket(IoUringSocketEntry& socket);

  // Inject a request completion into the io_uring instance for a specific socket.
  void injectCompletion(IoUringSocket& socket, Request::RequestType type, int32_t result);

  // Return the number of sockets in this worker.
  uint32_t getNumOfSockets() const override { return sockets_.size(); }

protected:
  // Add a socket to the worker.
  IoUringSocketEntry& addSocket(IoUringSocketEntryPtr&& socket);
  void onFileEvent();
  void submit();

  // The io_uring instance.
  IoUringPtr io_uring_;
  const uint32_t accept_size_;
  const uint32_t read_buffer_size_;
  const uint32_t write_timeout_ms_;
  // The dispatcher of this worker is running on.
  Event::Dispatcher& dispatcher_;
  // The file event of io_uring's eventfd.
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
  IoUringSocketEntry(os_fd_t fd, IoUringWorkerImpl& parent, Event::FileReadyCb cb,
                     bool enable_close_event);

  // IoUringSocket
  IoUringWorker& getIoUringWorker() const override { return parent_; }
  os_fd_t fd() const override { return fd_; }

  void close(bool keep_fd_open, IoUringSocketOnClosedCb cb = nullptr) override {
    status_ = Closed;
    on_closed_cb_ = cb;
    // When keep_fd_open is true, the IoHandle needn't cleanup the
    // reference to the IoUringSocket. The new IoUringSocket will be
    // replaced with it.
    if (!keep_fd_open) {
      onLocalClose();
    }
  }
  void enable() override { status_ = Enabled; }
  void disable() override { status_ = Disabled; }
  void enableCloseEvent(bool enable) override { enable_close_event_ = enable; }
  void connect(const Network::Address::InstanceConstSharedPtr&) override { PANIC("not implement"); }
  void write(Buffer::Instance&) override { PANIC("not implement"); }
  uint64_t write(const Buffer::RawSlice*, uint64_t) override { PANIC("not implement"); }
  void shutdown(int) override { PANIC("not implement"); }

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
    if (on_closed_cb_) {
      on_closed_cb_();
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
  void injectCompletion(Request::RequestType type) override;

  IoUringSocketStatus getStatus() const override { return status_; }

  const OptRef<ReadParam>& getReadParam() const override { return read_param_; }
  const OptRef<AcceptedSocketParam>& getAcceptedSocketParam() const override {
    return accepted_socket_param_;
  }
  const OptRef<WriteParam>& getWriteParam() const override { return write_param_; }

  void clearAcceptedSocketParam() override { accepted_socket_param_ = absl::nullopt; }

  void setFileReadyCb(Event::FileReadyCb cb) override { cb_ = std::move(cb); }

  void onAcceptCompleted();
  void onReadCompleted();
  void onWriteCompleted();
  void onRemoteClose();
  void onLocalClose();

protected:
  // This will cleanup all the injected completions for this socket and
  // unlink itself from the worker.
  void cleanup();
  os_fd_t fd_{INVALID_SOCKET};
  IoUringWorkerImpl& parent_;
  uint32_t injected_completions_{0};
  IoUringSocketStatus status_{Initialized};
  bool enable_close_event_{false};
  IoUringSocketOnClosedCb on_closed_cb_{nullptr};

  OptRef<ReadParam> read_param_;
  OptRef<AcceptedSocketParam> accepted_socket_param_;
  OptRef<WriteParam> write_param_;

  Event::FileReadyCb cb_;
};

class IoUringAcceptSocket : public IoUringSocketEntry {
public:
  IoUringAcceptSocket(os_fd_t fd, IoUringWorkerImpl& parent, Event::FileReadyCb cb,
                      uint32_t accept_size, bool enable_close_event);

  void close(bool keep_fd_open, IoUringSocketOnClosedCb cb = nullptr) override;
  void enable() override;
  void disable() override;
  void onClose(Request* req, int32_t result, bool injected) override;
  void onAccept(Request* req, int32_t result, bool injected) override;

private:
  void close(bool keep_fd_open, IoUringSocketOnClosedCb cb, bool posted);

  const uint32_t accept_size_{0};
  // These are used to track the current submitted accept requests.
  std::vector<Request*> requests_;
  size_t request_count_{0};
  bool closed_{false};

  void submitRequests();
};

class IoUringServerSocket : public IoUringSocketEntry {
public:
  IoUringServerSocket(os_fd_t fd, IoUringWorkerImpl& parent, Event::FileReadyCb cb,
                      uint32_t write_timeout_ms, bool enable_close_event);
  IoUringServerSocket(os_fd_t fd, Buffer::Instance& read_buf, IoUringWorkerImpl& parent,
                      Event::FileReadyCb cb, uint32_t write_timeout_ms, bool enable_close_event);
  ~IoUringServerSocket() override;

  // IoUringSocket
  void close(bool keep_fd_open, IoUringSocketOnClosedCb cb = nullptr) override;
  void enable() override;
  void disable() override;
  void write(Buffer::Instance& data) override;
  uint64_t write(const Buffer::RawSlice* slices, uint64_t num_slice) override;
  void shutdown(int how) override;
  void onClose(Request* req, int32_t result, bool injected) override;
  void onRead(Request* req, int32_t result, bool injected) override;
  void onWrite(Request* req, int32_t result, bool injected) override;
  void onShutdown(Request* req, int32_t result, bool injected) override;
  void onCancel(Request* req, int32_t result, bool injected) override;

  Buffer::OwnedImpl& getReadBuffer() { return read_buf_; }

protected:
  const uint32_t write_timeout_ms_;
  // For read. io_uring socket will read sequentially in the order of buf_ and read_error_. Unless
  // the buf_ is empty, the read_error_ will not be past to the handler. There is an exception that
  // when enable_close_event_ is set, the remote close read_error_(0) will always be past to the
  // handler.
  Request* read_req_{};
  // TODO (soulxu): Add water mark here.
  Buffer::OwnedImpl read_buf_;
  // TODO (soulxu): using queue for completion.
  absl::optional<int32_t> read_error_;

  // TODO (soulxu): We need water mark for write buffer.
  // The upper layer will think the buffer released when the data copy into this write buffer.
  // This leads to the `IntegrationTest.TestFloodUpstreamErrors` timeout, since the http layer
  // always think the response is write successful, so flood protection is never kicked.
  //
  // For write. io_uring socket will write sequentially in the order of write_buf_ and shutdown_
  // Unless the write_buf_ is empty, the shutdown operation will not be performed.
  Buffer::OwnedImpl write_buf_;
  // shutdown_ has 3 states. A absl::nullopt indicates the socket has not been shutdown, a false
  // value represents the socket wants to be shutdown but the shutdown has not been performed or
  // completed, and a true value means the socket has been shutdown.
  absl::optional<bool> shutdown_{};
  // If there is in progress write_or_shutdown_req_ during closing, a write timeout timer may be
  // setup to cancel the write_or_shutdown_req_, either a write request or a shutdown request. So
  // we can make sure all SQEs bounding to the io_uring socket is completed and the socket can be
  // closed successfully.
  Request* write_or_shutdown_req_{nullptr};
  Event::TimerPtr write_timeout_timer_{nullptr};

  bool keep_fd_open_{false};

  Request* cancel_req_{nullptr};
  Request* write_or_shutdown_cancel_req_{nullptr};
  Request* close_req_{nullptr};

  void closeInternal();
  void submitReadRequest();
  void submitWriteOrShutdownRequest();
};

class IoUringClientSocket : public IoUringServerSocket {
public:
  IoUringClientSocket(os_fd_t fd, IoUringWorkerImpl& parent, Event::FileReadyCb cb,
                      uint32_t write_timeout_ms, bool enable_close_event);

  void connect(const Network::Address::InstanceConstSharedPtr& address) override;
  void onConnect(Request* req, int32_t result, bool injected) override;
};

} // namespace Io
} // namespace Envoy
