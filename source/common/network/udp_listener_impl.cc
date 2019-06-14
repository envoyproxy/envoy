#include "common/network/udp_listener_impl.h"

#include <sys/un.h>

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/common/stack_array.h"
#include "common/event/dispatcher_impl.h"
#include "common/network/address_impl.h"

#include "event2/listener.h"

#define ENVOY_UDP_LOG(LEVEL, FORMAT, ...)                                                          \
  ENVOY_LOG_TO_LOGGER(ENVOY_LOGGER(), LEVEL, "Listener at {} :" FORMAT,                            \
                      this->localAddress()->asString(), ##__VA_ARGS__)

namespace Envoy {
namespace Network {

UdpListenerImpl::UdpListenerImpl(Event::DispatcherImpl& dispatcher, Socket& socket,
                                 UdpListenerCallbacks& cb)
    : BaseListenerImpl(dispatcher, socket), cb_(cb) {
  file_event_ = dispatcher_.createFileEvent(
      socket.ioHandle().fd(), [this](uint32_t events) -> void { onSocketEvent(events); },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read | Event::FileReadyType::Write);

  ASSERT(file_event_);

  if (!Network::Socket::applyOptions(socket.options(), socket,
                                     envoy::api::v2::core::SocketOption::STATE_BOUND)) {
    throw CreateListenerException(fmt::format("cannot set post-bound socket option on socket: {}",
                                              socket.localAddress()->asString()));
  }
}

UdpListenerImpl::~UdpListenerImpl() {
  disable();
  file_event_.reset();
}

void UdpListenerImpl::disable() { file_event_->setEnabled(0); }

void UdpListenerImpl::enable() {
  file_event_->setEnabled(Event::FileReadyType::Read | Event::FileReadyType::Write);
}

UdpListenerImpl::ReceiveResult UdpListenerImpl::doRecvFrom(sockaddr_storage& peer_addr,
                                                           socklen_t& addr_len) {
  constexpr uint64_t const read_length = 16384;

  Buffer::InstancePtr buffer = std::make_unique<Buffer::OwnedImpl>();

  addr_len = sizeof(sockaddr_storage);
  memset(&peer_addr, 0, addr_len);

  Buffer::RawSlice slice;
  const uint64_t num_slices = buffer->reserve(read_length, &slice, 1);

  ASSERT(num_slices == 1);

  auto& os_sys_calls = Api::OsSysCallsSingleton::get();
  // TODO(sumukhs) - Convert this to use the IOHandle interface rather than os_sys_calls
  const Api::SysCallSizeResult result =
      os_sys_calls.recvfrom(socket_.ioHandle().fd(), slice.mem_, read_length, 0,
                            reinterpret_cast<struct sockaddr*>(&peer_addr), &addr_len);
  if (result.rc_ < 0) {
    return ReceiveResult{Api::SysCallIntResult{static_cast<int>(result.rc_), result.errno_},
                         nullptr};
  }
  slice.len_ = std::min(slice.len_, static_cast<size_t>(result.rc_));
  buffer->commit(&slice, 1);

  ENVOY_UDP_LOG(trace, "recvfrom bytes {}", result.rc_);
  return ReceiveResult{Api::SysCallIntResult{static_cast<int>(result.rc_), 0}, std::move(buffer)};
}

void UdpListenerImpl::onSocketEvent(short flags) {
  ASSERT((flags & (Event::FileReadyType::Read | Event::FileReadyType::Write)));
  ENVOY_UDP_LOG(trace, "socket event: {}", flags);

  if (flags & Event::FileReadyType::Read) {
    handleReadCallback();
  }

  if (flags & Event::FileReadyType::Write) {
    handleWriteCallback();
  }
}

void UdpListenerImpl::handleReadCallback() {
  ENVOY_UDP_LOG(trace, "handleReadCallback");
  sockaddr_storage addr;
  socklen_t addr_len = 0;

  do {
    ReceiveResult recv_result = doRecvFrom(addr, addr_len);
    if ((recv_result.result_.rc_ < 0)) {
      if (recv_result.result_.errno_ != EAGAIN) {
        ENVOY_UDP_LOG(error, "recvfrom result {}", recv_result.result_.errno_);
        cb_.onReceiveError(UdpListenerCallbacks::ErrorCode::SyscallError,
                           recv_result.result_.errno_);
      }
      return;
    }

    if (recv_result.result_.rc_ == 0) {
      // TODO(conqerAtapple): Is zero length packet interesting?
      return;
    }

    Address::InstanceConstSharedPtr local_address = socket_.localAddress();

    RELEASE_ASSERT(
        addr_len > 0,
        fmt::format(
            "Unable to get remote address for fd: {}, local address: {}. address length is 0 ",
            socket_.ioHandle().fd(), local_address->asString()));

    Address::InstanceConstSharedPtr peer_address;

    try {
      peer_address = Address::addressFromSockAddr(
          addr, addr_len, local_address->ip()->version() == Address::IpVersion::v6);
    } catch (const EnvoyException&) {
      // Intentional no-op. The assert should fail below
    }

    RELEASE_ASSERT((peer_address != nullptr),
                   fmt::format("Unable to get remote address for fd: {}, local address: {} ",
                               socket_.ioHandle().fd(), local_address->asString()));

    // Unix domain sockets are not supported
    RELEASE_ASSERT(peer_address->type() == Address::Type::Ip,
                   fmt::format("Unsupported peer address: {} local address: {}, receive size: "
                               "{}, address length: {}",
                               peer_address->asString(), local_address->asString(),
                               recv_result.result_.rc_, addr_len));

    UdpRecvData recvData{local_address, peer_address, std::move(recv_result.buffer_)};
    cb_.onData(recvData);

  } while (true);
}

void UdpListenerImpl::handleWriteCallback() {
  ENVOY_UDP_LOG(trace, "handleWriteCallback");
  cb_.onWriteReady(socket_);
}

Event::Dispatcher& UdpListenerImpl::dispatcher() { return dispatcher_; }

const Address::InstanceConstSharedPtr& UdpListenerImpl::localAddress() const {
  return socket_.localAddress();
}

Api::IoCallUint64Result UdpListenerImpl::send(const UdpSendData& send_data) {
  ENVOY_UDP_LOG(trace, "send");
  Buffer::Instance& buffer = send_data.buffer_;
  uint64_t num_slices = buffer.getRawSlices(nullptr, 0);
  STACK_ARRAY(slices, Buffer::RawSlice, num_slices);
  buffer.getRawSlices(slices.begin(), num_slices);
  Api::IoCallUint64Result send_result =
      socket_.ioHandle().sendmsg(slices.begin(), num_slices, 0, *send_data.send_address_);

  if (send_result.ok()) {
    ASSERT(send_result.rc_ == buffer.length());
    ENVOY_UDP_LOG(trace, "sendmsg sent:{} bytes", send_result.rc_);
  } else {
    ENVOY_UDP_LOG(debug, "sendmsg failed with error {}. Ret {}",
                  static_cast<int>(send_result.err_->getErrorCode()), send_result.rc_);
  }

  // The send_result normalizes the rc_ value to 0 in error conditions.
  // The drain call is hence 'safe' in success and failure cases.
  buffer.drain(send_result.rc_);

  return send_result;
}

} // namespace Network
} // namespace Envoy
