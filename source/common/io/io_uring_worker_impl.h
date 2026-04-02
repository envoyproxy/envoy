#pragma once

#include "envoy/common/io/io_uring.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/linked_object.h"
#include "source/common/common/logger.h"
#include "source/common/io/io_uring_impl.h"

namespace Envoy {
namespace Io {

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
  IoUringWorkerImpl(uint32_t io_uring_size, bool use_submission_queue_polling,
                    uint32_t read_buffer_size, uint32_t write_timeout_ms,
                    uint32_t write_high_watermark_bytes, uint32_t write_low_watermark_bytes,
                    Event::Dispatcher& dispatcher);
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
  Request* submitWriteRequest(IoUringSocket& socket, const Buffer::RawSliceVector& slices) override;
  Request* submitCloseRequest(IoUringSocket& socket) override;
  Request* submitCancelRequest(IoUringSocket& socket, Request* request_to_cancel) override;
  Request* submitShutdownRequest(IoUringSocket& socket, int how) override;

  Event::Dispatcher& dispatcher() override;

  IoUringSocketEntryPtr removeSocket(IoUringSocketEntry& socket);
  void injectCompletion(IoUringSocket& socket, Request::RequestType type, int32_t result);

  uint32_t getNumOfSockets() const override { return sockets_.size(); }

protected:
  IoUringSocketEntry& addSocket(IoUringSocketEntryPtr&& socket);
  void onFileEvent();
  void submit();

  IoUringPtr io_uring_;
  const uint32_t read_buffer_size_;
  const uint32_t write_timeout_ms_;
  const uint32_t write_high_watermark_bytes_;
  const uint32_t write_low_watermark_bytes_;
  Event::Dispatcher& dispatcher_;
  Event::FileEventPtr file_event_{nullptr};
  std::list<IoUringSocketEntryPtr> sockets_;
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
  void cleanup();
  void onReadCompleted();
  void onWriteCompleted();
  void onRemoteClose();

  os_fd_t fd_{INVALID_SOCKET};
  IoUringWorkerImpl& parent_;
  uint8_t injected_completions_{0};
  IoUringSocketStatus status_{Initialized};
  bool enable_close_event_{false};
  IoUringSocketOnClosedCb on_closed_cb_{nullptr};
  OptRef<ReadParam> read_param_;
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
  const uint32_t write_timeout_ms_;
  const uint32_t write_high_watermark_bytes_;
  const uint32_t write_low_watermark_bytes_;

  Request* read_req_{};
  Buffer::OwnedImpl read_buf_;
  absl::optional<int32_t> read_error_;

  Buffer::OwnedImpl write_buf_;
  absl::optional<bool> shutdown_{};
  Request* write_or_shutdown_req_{nullptr};
  Event::TimerPtr write_timeout_timer_{nullptr};
  bool keep_fd_open_{false};
  Request* read_cancel_req_{nullptr};
  Request* write_or_shutdown_cancel_req_{nullptr};
  Request* close_req_{nullptr};
  // Tracks write buffer backpressure state for future upstream read throttling.
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
