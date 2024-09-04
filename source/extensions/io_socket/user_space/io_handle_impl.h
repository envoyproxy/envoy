#pragma once

#include <memory>

#include "envoy/api/io_error.h"
#include "envoy/api/os_sys_calls.h"
#include "envoy/common/platform.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/address.h"
#include "envoy/network/io_handle.h"

#include "source/common/buffer/watermark_buffer.h"
#include "source/common/common/logger.h"
#include "source/common/network/io_socket_error_impl.h"
#include "source/extensions/io_socket/user_space/file_event_impl.h"
#include "source/extensions/io_socket/user_space/io_handle.h"

namespace Envoy {
namespace Extensions {
namespace IoSocket {
namespace UserSpace {

// Align with Buffer::Reservation::MAX_SLICES_
constexpr uint32_t MAX_FRAGMENT = 8;
// Align with Buffer::Slice::default_slice_size_
constexpr uint64_t FRAGMENT_SIZE = 16 * 1024;

/**
 * Network::IoHandle implementation which provides a buffer as data source. It is designed to used
 * by Network::ConnectionImpl. Some known limitations include
 * 1. It doesn't include a file descriptor. Do not use "fdDoNotUse".
 * 2. It doesn't support socket options. Wrap this in ConnectionSocket and implement the socket
 * getter/setter options.
 * 3. It doesn't support UDP interface.
 * 4. The peer BufferedIoSocket must be scheduled in the same thread to avoid data race because
 *    IoHandleImpl mutates the state of peer handle and no lock is introduced.
 */
class IoHandleImpl final : public Network::IoHandle,
                           public UserSpace::IoHandle,
                           protected Logger::Loggable<Logger::Id::io>,
                           NonCopyable {
public:
  ~IoHandleImpl() override;

  // Network::IoHandle
  os_fd_t fdDoNotUse() const override {
    ASSERT(false, "not supported");
    return INVALID_SOCKET;
  }
  Api::IoCallUint64Result close() override;
  bool isOpen() const override;
  bool wasConnected() const override;
  Api::IoCallUint64Result readv(uint64_t max_length, Buffer::RawSlice* slices,
                                uint64_t num_slice) override;
  Api::IoCallUint64Result read(Buffer::Instance& buffer,
                               absl::optional<uint64_t> max_length_opt) override;
  Api::IoCallUint64Result writev(const Buffer::RawSlice* slices, uint64_t num_slice) override;
  Api::IoCallUint64Result write(Buffer::Instance& buffer) override;
  Api::IoCallUint64Result sendmsg(const Buffer::RawSlice* slices, uint64_t num_slice, int flags,
                                  const Network::Address::Ip* self_ip,
                                  const Network::Address::Instance& peer_address) override;
  Api::IoCallUint64Result recvmsg(Buffer::RawSlice* slices, const uint64_t num_slice,
                                  uint32_t self_port,
                                  const Network::IoHandle::UdpSaveCmsgConfig& udp_save_cmsg_config,
                                  RecvMsgOutput& output) override;
  Api::IoCallUint64Result recvmmsg(RawSliceArrays& slices, uint32_t self_port,
                                   const Network::IoHandle::UdpSaveCmsgConfig& udp_save_cmsg_config,
                                   RecvMsgOutput& output) override;
  Api::IoCallUint64Result recv(void* buffer, size_t length, int flags) override;
  bool supportsMmsg() const override;
  bool supportsUdpGro() const override;
  Api::SysCallIntResult bind(Network::Address::InstanceConstSharedPtr address) override;
  Api::SysCallIntResult listen(int backlog) override;
  Network::IoHandlePtr accept(struct sockaddr* addr, socklen_t* addrlen) override;
  Api::SysCallIntResult connect(Network::Address::InstanceConstSharedPtr address) override;
  Api::SysCallIntResult setOption(int level, int optname, const void* optval,
                                  socklen_t optlen) override;
  Api::SysCallIntResult getOption(int level, int optname, void* optval, socklen_t* optlen) override;
  Api::SysCallIntResult ioctl(unsigned long, void*, unsigned long, void*, unsigned long,
                              unsigned long*) override;
  Api::SysCallIntResult setBlocking(bool blocking) override;
  absl::optional<int> domain() override;
  Network::Address::InstanceConstSharedPtr localAddress() override;
  Network::Address::InstanceConstSharedPtr peerAddress() override;

  void initializeFileEvent(Event::Dispatcher& dispatcher, Event::FileReadyCb cb,
                           Event::FileTriggerType trigger, uint32_t events) override;
  Network::IoHandlePtr duplicate() override;
  void activateFileEvents(uint32_t events) override;
  void enableFileEvents(uint32_t events) override;
  void resetFileEvents() override;

  Api::SysCallIntResult shutdown(int how) override;
  absl::optional<std::chrono::milliseconds> lastRoundTripTime() override { return absl::nullopt; }
  absl::optional<uint64_t> congestionWindowInBytes() const override { return absl::nullopt; }
  absl::optional<std::string> interfaceName() override { return absl::nullopt; }

  void setWatermarks(uint32_t watermark) { pending_received_data_.setWatermarks(watermark); }
  void onBelowLowWatermark() {
    if (peer_handle_) {
      ENVOY_LOG(debug, "Socket {} switches to low watermark. Notify {}.", static_cast<void*>(this),
                static_cast<void*>(peer_handle_));
      peer_handle_->onPeerBufferLowWatermark();
    }
  }
  void onAboveHighWatermark() {
    // Low to high is checked by peer after peer writes data.
  }

  // UserSpace::IoHandle
  void setWriteEnd() override {
    receive_data_end_stream_ = true;
    setNewDataAvailable();
  }
  void setNewDataAvailable() override {
    ENVOY_LOG(trace, "{} on socket {}", __FUNCTION__, static_cast<void*>(this));
    if (user_file_event_) {
      user_file_event_->activateIfEnabled(
          Event::FileReadyType::Read |
          // Closed ready type is defined as `end of stream`
          (receive_data_end_stream_ ? Event::FileReadyType::Closed : 0));
    }
  }
  void onPeerDestroy() override {
    peer_handle_ = nullptr;
    write_shutdown_ = true;
  }
  void onPeerBufferLowWatermark() override {
    if (user_file_event_) {
      user_file_event_->activateIfEnabled(Event::FileReadyType::Write);
    }
  }
  bool isWritable() const override { return !pending_received_data_.highWatermarkTriggered(); }
  bool isPeerShutDownWrite() const override { return receive_data_end_stream_; }
  bool isPeerWritable() const override {
    return peer_handle_ != nullptr && !peer_handle_->isPeerShutDownWrite() &&
           peer_handle_->isWritable();
  }
  Buffer::Instance* getWriteBuffer() override { return &pending_received_data_; }

  // `UserspaceIoHandle`
  bool isReadable() const override {
    return isPeerShutDownWrite() || pending_received_data_.length() > 0;
  }

  // Set the peer which will populate the owned pending_received_data.
  void setPeerHandle(UserSpace::IoHandle* writable_peer) {
    // Swapping writable peer is undefined behavior.
    ASSERT(!peer_handle_);
    ASSERT(!write_shutdown_);
    peer_handle_ = writable_peer;
    ENVOY_LOG(trace, "io handle {} set peer handle to {}.", static_cast<void*>(this),
              static_cast<void*>(writable_peer));
  }

  PassthroughStateSharedPtr passthroughState() override { return passthrough_state_; }

private:
  friend class IoHandleFactory;
  explicit IoHandleImpl(PassthroughStateSharedPtr passthrough_state = nullptr);

  static const Network::Address::InstanceConstSharedPtr& getCommonInternalAddress();

  // Support isOpen() and close(). Network::IoHandle owner must invoke close() to avoid potential
  // resource leak.
  bool closed_{false};

  // The attached file event with this socket.
  std::unique_ptr<FileEventImpl> user_file_event_;

  // True if pending_received_data_ is not addable. Note that pending_received_data_ may have
  // pending data to drain.
  bool receive_data_end_stream_{false};

  // The buffer owned by this socket. This buffer is populated by the write operations of the peer
  // socket and drained by read operations of this socket.
  Buffer::WatermarkBuffer pending_received_data_;

  // Destination of the write(). The value remains non-null until the peer is closed.
  UserSpace::IoHandle* peer_handle_{nullptr};

  // The flag whether the peer is valid. Any write attempt must check this flag.
  bool write_shutdown_{false};

  // Shared state between peer handles.
  PassthroughStateSharedPtr passthrough_state_{nullptr};
};

class PassthroughStateImpl : public PassthroughState, public Logger::Loggable<Logger::Id::io> {
public:
  void initialize(std::unique_ptr<envoy::config::core::v3::Metadata> metadata,
                  const StreamInfo::FilterState::Objects& filter_state_objects) override;
  void mergeInto(envoy::config::core::v3::Metadata& metadata,
                 StreamInfo::FilterState& filter_state) override;

private:
  enum class State { Created, Initialized, Done };
  State state_{State::Created};
  std::unique_ptr<envoy::config::core::v3::Metadata> metadata_;
  StreamInfo::FilterState::Objects filter_state_objects_;
};

using IoHandleImplPtr = std::unique_ptr<IoHandleImpl>;
class IoHandleFactory {
public:
  static std::pair<IoHandleImplPtr, IoHandleImplPtr> createIoHandlePair() {
    auto state = std::make_shared<PassthroughStateImpl>();
    auto p = std::pair<IoHandleImplPtr, IoHandleImplPtr>{new IoHandleImpl(state),
                                                         new IoHandleImpl(state)};
    p.first->setPeerHandle(p.second.get());
    p.second->setPeerHandle(p.first.get());
    return p;
  }
  static std::pair<IoHandleImplPtr, IoHandleImplPtr>
  createBufferLimitedIoHandlePair(uint32_t buffer_size) {
    auto state = std::make_shared<PassthroughStateImpl>();
    auto p = std::pair<IoHandleImplPtr, IoHandleImplPtr>{new IoHandleImpl(state),
                                                         new IoHandleImpl(state)};
    // This buffer watermark setting emulates the OS socket buffer parameter
    // `/proc/sys/net/ipv4/tcp_{r,w}mem`.
    p.first->setWatermarks(buffer_size);
    p.second->setWatermarks(buffer_size);
    p.first->setPeerHandle(p.second.get());
    p.second->setPeerHandle(p.first.get());
    return p;
  }
};
} // namespace UserSpace
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
