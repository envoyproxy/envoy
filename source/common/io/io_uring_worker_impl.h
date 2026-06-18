#pragma once

#include "envoy/common/io/io_uring.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/linked_object.h"
#include "source/common/common/logger.h"
#include "source/common/io/io_uring_impl.h"

#include "absl/container/inlined_vector.h"

namespace Envoy {
namespace Io {

class ReadRequest : public Request {
public:
  ReadRequest(IoUringSocket& socket, uint32_t size);

  std::unique_ptr<uint8_t[]> buf_;
  struct iovec iov_;
};

class WriteRequest : public Request {
public:
  WriteRequest(IoUringSocket& socket, const Buffer::RawSliceVector& slices);

  // Inline storage matches the RawSliceVector capacity, so a typical write keeps its iovecs inside
  // this request and avoids a second heap allocation per write.
  absl::InlinedVector<struct iovec, 16> iov_;
};

class IoUringSocketEntry;
using IoUringSocketEntryPtr = std::unique_ptr<IoUringSocketEntry>;

class IoUringWorkerImpl : public IoUringWorker, private Logger::Loggable<Logger::Id::io> {
public:
  IoUringWorkerImpl(uint32_t io_uring_size, bool use_submission_queue_polling,
                    bool enable_multishot_receive, uint32_t read_buffer_size,
                    uint32_t write_timeout_ms, uint32_t write_high_watermark_bytes,
                    uint32_t write_low_watermark_bytes, Event::Dispatcher& dispatcher);
  IoUringWorkerImpl(IoUringPtr&& io_uring, uint32_t read_buffer_size, uint32_t write_timeout_ms,
                    uint32_t write_high_watermark_bytes, uint32_t write_low_watermark_bytes,
                    Event::Dispatcher& dispatcher);
  ~IoUringWorkerImpl() override;

  // IoUringWorker
  IoUringSocket& addServerSocket(os_fd_t fd, Event::FileReadyCb cb,
                                 bool enable_close_event) override;
  IoUringSocket& addServerSocket(os_fd_t fd, Buffer::Instance& read_buf, Event::FileReadyCb cb,
                                 bool enable_close_event) override;
  IoUringSocket& addClientSocket(os_fd_t fd, Event::FileReadyCb cb,
                                 bool enable_close_event) override;

  Request* submitConnectRequest(IoUringSocket& socket,
                                const Network::Address::InstanceConstSharedPtr& address) override;
  Request* submitReadRequest(IoUringSocket& socket) override;
  // Submit a read request using an explicit read size to support adaptive read sizing.
  Request* submitReadRequest(IoUringSocket& socket, uint32_t read_size);
  // Submit a `multishot` read request that draws buffers from the provided buffer pool.
  Request* submitReadMultishotRequest(IoUringSocket& socket);
  Request* submitWriteRequest(IoUringSocket& socket, const Buffer::RawSliceVector& slices) override;
  Request* submitCloseRequest(IoUringSocket& socket) override;
  Request* submitCancelRequest(IoUringSocket& socket, Request* request_to_cancel) override;
  Request* submitShutdownRequest(IoUringSocket& socket, int how) override;

  Event::Dispatcher& dispatcher() override;

  // Remove a socket from this worker.
  IoUringSocketEntryPtr removeSocket(IoUringSocketEntry& socket);

  // Inject a request completion into the iouring instance for a specific socket.
  void injectCompletion(IoUringSocket& socket, Request::RequestType type, int32_t result);

  // Return the number of sockets in this worker.
  uint32_t getNumOfSockets() const override { return sockets_.size(); }

  // Return the configured base read buffer size used as the starting point for adaptive reads.
  uint32_t readBufferSize() const { return read_buffer_size_; }

  // Whether this worker's io_uring has `multishot` reads backed by a provided buffer pool
  // available.
  bool isMultishotEnabled() const { return multishot_enabled_; }
  // The provided buffer pool used for `multishot` reads, or nullptr when `multishot` reads are not
  // available. Cached at construction to avoid a virtual call on each read completion.
  const IoUringBufferPoolSharedPtr& bufferPool() const { return buffer_pool_; }

protected:
  // Add a socket to the worker.
  IoUringSocketEntry& addSocket(IoUringSocketEntryPtr&& socket);
  void onFileEvent();
  void submit();

  // The iouring instance.
  IoUringPtr io_uring_;
  // Whether `multishot` reads are available, cached once at construction to avoid a virtual call on
  // each read submission.
  const bool multishot_enabled_;
  // The provided buffer pool used for `multishot` reads, cached once at construction. Null when
  // `multishot` reads are not available.
  const IoUringBufferPoolSharedPtr buffer_pool_;
  const uint32_t read_buffer_size_;
  const uint32_t write_timeout_ms_;
  const uint32_t write_high_watermark_bytes_;
  const uint32_t write_low_watermark_bytes_;
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
  IoUringSocketEntry(os_fd_t fd, IoUringWorkerImpl& parent, Event::FileReadyCb cb,
                     bool enable_close_event);

  // IoUringSocket
  IoUringWorker& getIoUringWorker() const override { return parent_; }
  os_fd_t fd() const override { return fd_; }

  void close(bool, IoUringSocketOnClosedCb cb = nullptr) override {
    status_ = Closed;
    on_closed_cb_ = cb;
  }
  void enableRead() override { status_ = ReadEnabled; }
  void disableRead() override { status_ = ReadDisabled; }
  void enableCloseEvent(bool enable) override { enable_close_event_ = enable; }
  void connect(const Network::Address::InstanceConstSharedPtr&) override { PANIC("not implement"); }

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

  IoUringSocketStatus getStatus() const override { return status_; }

  const OptRef<ReadParam>& getReadParam() const override { return read_param_; }
  const OptRef<WriteParam>& getWriteParam() const override { return write_param_; }

  void setFileReadyCb(Event::FileReadyCb cb) override { cb_ = std::move(cb); }

protected:
  /**
   * For the socket to remove itself from the IoUringWorker and defer deletion.
   */
  void cleanup();
  void onReadCompleted();
  void onWriteCompleted();
  void onRemoteClose();

  os_fd_t fd_{INVALID_SOCKET};
  IoUringWorkerImpl& parent_;
  // This records already injected completion request type to
  // avoid duplicated injections.
  uint8_t injected_completions_{0};
  // The current status of socket.
  IoUringSocketStatus status_{Initialized};
  // Deliver the remote close as file read event or file close event.
  bool enable_close_event_{false};
  // The callback will be invoked when close request is done.
  IoUringSocketOnClosedCb on_closed_cb_{nullptr};
  // This object stores the data get from read request.
  OptRef<ReadParam> read_param_;
  // This object stores the data get from write request.
  OptRef<WriteParam> write_param_;

  Event::FileReadyCb cb_;
};

class IoUringServerSocket : public IoUringSocketEntry {
public:
  IoUringServerSocket(os_fd_t fd, IoUringWorkerImpl& parent, Event::FileReadyCb cb,
                      uint32_t write_timeout_ms, uint32_t write_high_watermark_bytes,
                      uint32_t write_low_watermark_bytes, bool enable_close_event);
  IoUringServerSocket(os_fd_t fd, Buffer::Instance& read_buf, IoUringWorkerImpl& parent,
                      Event::FileReadyCb cb, uint32_t write_timeout_ms,
                      uint32_t write_high_watermark_bytes, uint32_t write_low_watermark_bytes,
                      bool enable_close_event);
  ~IoUringServerSocket() override;

  // IoUringSocket
  void close(bool keep_fd_open, IoUringSocketOnClosedCb cb = nullptr) override;
  void enableRead() override;
  void disableRead() override;
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
  // Since the write of IoUringSocket is async, there may have write request is on the fly when
  // close the socket. This timeout is setting for a time to wait the write request done.
  const uint32_t write_timeout_ms_;
  // The write buffer high and low watermarks used to apply backpressure to the handler.
  const uint32_t write_high_watermark_bytes_;
  const uint32_t write_low_watermark_bytes_;
  // For read. iouring socket will read sequentially in the order of buf_ and read_error_. Unless
  // the buf_ is empty, the read_error_ will not be past to the handler. There is an exception that
  // when enable_close_event_ is set, the remote close read_error_(0) will always be past to the
  // handler.
  Request* read_req_{};
  // The next read size for adaptive read sizing. It starts at the base read buffer size, grows when
  // a read fills the buffer and resets to the base size when a read does not.
  uint32_t next_read_size_{0};
  // Whether the in-flight read request is a `multishot` read drawing from the provided buffer pool.
  bool read_is_multishot_{false};
  // Set when a `multishot` read ends with an exhausted buffer pool. The next read uses a readv that
  // re-arms `multishot` once buffers are recycled.
  bool multishot_fallback_{false};
  // Set when the kernel reports `multishot` recv is unsupported. The socket then uses readv only.
  bool multishot_disabled_{false};
  // TODO (soulxu): Add water mark here.
  Buffer::OwnedImpl read_buf_;
  absl::optional<int32_t> read_error_;

  // For write. iouring socket will write sequentially in the order of write_buf_ and shutdown_
  // Unless the write_buf_ is empty, the shutdown operation will not be performed.
  // Once write_buf_ grows above the high watermark the handler is told to stop writing, and it is
  // notified to resume after write_buf_ drains below the low watermark. This applies backpressure
  // to the upper layer so flood protection can kick in.
  Buffer::OwnedImpl write_buf_;
  // shutdown_ has 3 states. A absl::nullopt indicates the socket has not been shutdown, a false
  // value represents the socket wants to be shutdown but the shutdown has not been performed or
  // completed, and a true value means the socket has been shutdown.
  absl::optional<bool> shutdown_;
  // If there is in progress write_or_shutdown_req_ during closing, a write timeout timer may be
  // setup to cancel the write_or_shutdown_req_, either a write request or a shutdown request. So
  // we can make sure all SQEs bounding to the iouring socket is completed and the socket can be
  // closed successfully.
  Request* write_or_shutdown_req_{nullptr};
  Event::TimerPtr write_timeout_timer_{nullptr};
  // Whether keep the fd open when close the IoUringSocket.
  bool keep_fd_open_{false};
  // This is used for tracking the read's cancel request.
  Request* read_cancel_req_{nullptr};
  // This is used for tracking the write or shutdown's cancel request.
  Request* write_or_shutdown_cancel_req_{nullptr};
  // This is used for tracking the close request.
  Request* close_req_{nullptr};
  // Whether the write buffer is above the high watermark and backpressure is being applied.
  bool above_write_high_watermark_{false};

  void closeInternal();
  void submitReadRequest();
  void submitWriteOrShutdownRequest();
  void moveReadDataToBuffer(Request* req, size_t data_length);
  void onReadCompleted(int32_t result);
  void onWriteCompleted(int32_t result);
  void checkWriteWatermarks();
};

class IoUringClientSocket : public IoUringServerSocket {
public:
  IoUringClientSocket(os_fd_t fd, IoUringWorkerImpl& parent, Event::FileReadyCb cb,
                      uint32_t write_timeout_ms, uint32_t write_high_watermark_bytes,
                      uint32_t write_low_watermark_bytes, bool enable_close_event);

  void connect(const Network::Address::InstanceConstSharedPtr& address) override;
  void onConnect(Request* req, int32_t result, bool injected) override;
};

} // namespace Io
} // namespace Envoy
