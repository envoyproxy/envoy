#pragma once

#include "envoy/api/io_error.h"
#include "envoy/api/os_sys_calls.h"
#include "envoy/common/platform.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/io_handle.h"

#include "common/buffer/watermark_buffer.h"
#include "common/common/logger.h"
#include "common/network/io_socket_error_impl.h"
#include "common/network/peer_buffer.h"

namespace Envoy {
namespace Network {

/**
 * IoHandle implementation which provides a buffer as data source.
 */
class BufferedIoSocketHandleImpl : public IoHandle,
                                   public WritablePeer,
                                   public ReadableSource,
                                   protected Logger::Loggable<Logger::Id::io> {
public:
  BufferedIoSocketHandleImpl()
      : closed_{false}, owned_buffer_(
                            [this]() -> void {
                              over_high_watermark_ = false;
                              triggered_high_to_low_watermark_ = true;
                            },
                            [this]() -> void { over_high_watermark_ = true; }, []() -> void {}) {}

  ~BufferedIoSocketHandleImpl() override { ASSERT(closed_); }

  // IoHandle
  os_fd_t fdDoNotUse() const override { return INVALID_SOCKET; }

  Api::IoCallUint64Result close() override;

  bool isOpen() const override;

  Api::IoCallUint64Result readv(uint64_t max_length, Buffer::RawSlice* slices,
                                uint64_t num_slice) override;

  Api::IoCallUint64Result writev(const Buffer::RawSlice* slices, uint64_t num_slice) override;

  Api::IoCallUint64Result sendmsg(const Buffer::RawSlice* slices, uint64_t num_slice, int flags,
                                  const Address::Ip* self_ip,
                                  const Address::Instance& peer_address) override;

  Api::IoCallUint64Result recvmsg(Buffer::RawSlice* slices, const uint64_t num_slice,
                                  uint32_t self_port, RecvMsgOutput& output) override;

  Api::IoCallUint64Result recvmmsg(RawSliceArrays& slices, uint32_t self_port,
                                   RecvMsgOutput& output) override;
  Api::IoCallUint64Result recv(void* buffer, size_t length, int flags) override;

  bool supportsMmsg() const override;
  bool supportsUdpGro() const override;

  Api::SysCallIntResult bind(Address::InstanceConstSharedPtr address) override;
  Api::SysCallIntResult listen(int backlog) override;
  IoHandlePtr accept(struct sockaddr* addr, socklen_t* addrlen) override;
  Api::SysCallIntResult connect(Address::InstanceConstSharedPtr address) override;
  Api::SysCallIntResult setOption(int level, int optname, const void* optval,
                                  socklen_t optlen) override;
  Api::SysCallIntResult getOption(int level, int optname, void* optval, socklen_t* optlen) override;
  Api::SysCallIntResult setBlocking(bool blocking) override;
  absl::optional<int> domain() override;
  Address::InstanceConstSharedPtr localAddress() override;
  Address::InstanceConstSharedPtr peerAddress() override;
  Event::FileEventPtr createFileEvent(Event::Dispatcher& dispatcher, Event::FileReadyCb cb,
                                      Event::FileTriggerType trigger, uint32_t events) override;
  Api::SysCallIntResult shutdown(int how) override;

  Buffer::WatermarkBuffer& getBufferForTest() { return owned_buffer_; }
  void scheduleWriteEvent() {}
  void scheduleNextEvent() {}

  void setWritablePeer(WritablePeer* writable_peer) {
    // Swapping writable peer is undefined behavior.
    ASSERT(!writable_peer_);
    ASSERT(!peer_closed_);
    writable_peer_ = writable_peer;
  }

  // WritablePeer
  void setWriteEnd() override { read_end_stream_ = true; }
  void maybeSetNewData() override {
    scheduleWriteEvent();
    scheduleNextEvent();
  }
  void onPeerDestroy() override {
    writable_peer_ = nullptr;
    peer_closed_ = true;
  }
  bool isWritable() const override {
    return !isOverHighWatermark();
  }
  Buffer::Instance* getWriteBuffer() override { return &owned_buffer_; }
  // ReadableSource
  bool isPeerShutDownWrite() const override { return read_end_stream_; }
  bool isOverHighWatermark() const override { return over_high_watermark_; }
  bool isReadable() const override { return isPeerShutDownWrite() || owned_buffer_.length() > 0; }

private:
  // Support isOpen() and close(). IoHandle owner must invoke close() to avoid potential resource
  // leak.
  bool closed_;
  // True if owned_buffer_ is not addable. Note that owned_buffer_ may have pending data to drain.
  bool read_end_stream_{false};
  Buffer::WatermarkBuffer owned_buffer_;

  // bool shutdown_{false};
  // Destination of the write().
  WritablePeer* writable_peer_{nullptr};

  // The flag whether the peer is valid. Any write attempt should check flag.
  bool peer_closed_{false};

  bool over_high_watermark_{false};
  bool triggered_high_to_low_watermark_{true};
};

} // namespace Network
} // namespace Envoy