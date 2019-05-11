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
        cb_.onError(UdpListenerCallbacks::ErrorCode::SyscallError, recv_result.result_.errno_);
      }
      return;
    }

    if (recv_result.result_.rc_ == 0) {
      // TODO(conqerAtapple): Is zero length packet interesting?
      return;
    }

    Address::InstanceConstSharedPtr local_address = socket_.localAddress();

    RELEASE_ASSERT((local_address != nullptr),
                   fmt::format("Unable to get local address for fd: {}", socket_.ioHandle().fd()));

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

    UdpRecvData recvData = {peer_address, std::move(recv_result.buffer_)};
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

void UdpListenerImpl::send(const UdpSendData& send_data) {
  ENVOY_UDP_LOG(trace, "send");
  Buffer::Instance& buffer = send_data.buffer_;
  uint64_t num_slices = buffer.getRawSlices(nullptr, 0);
  STACK_ARRAY(slices, Buffer::RawSlice, num_slices);
  buffer.getRawSlices(slices.begin(), num_slices);
  writev(slices.begin(), num_slices);
}

void UdpListenerImpl::writev(const Buffer::RawSlice* slices, uint64_t num_slice) {
  STACK_ARRAY(iov, iovec, num_slice);
  uint64_t num_slices_to_write = 0;
  uint64_t requested_send_size = 0;
  for (uint64_t i = 0; i < num_slice; i++) {
    if (slices[i].mem_ != nullptr && slices[i].len_ != 0) {
      iov[num_slices_to_write].iov_base = slices[i].mem_;
      iov[num_slices_to_write].iov_len = slices[i].len_;
      num_slices_to_write++;
      requested_send_size += slices[i].len_;
    }
  }
  if (num_slices_to_write == 0) {
    ENVOY_UDP_LOG(trace, "Nothing to send");
    return;
  }

  auto& os_syscalls = Api::OsSysCallsSingleton::get();
  const Api::SysCallSizeResult result =
      os_syscalls.writev(socket_.ioHandle().fd(), iov.begin(), num_slices_to_write);
  if (result.rc_ < 0) {
    if (result.errno_ == EAGAIN) {
      ENVOY_UDP_LOG(debug, "writev dropped {} bytes", requested_send_size);
    } else {
      ENVOY_UDP_LOG(debug, "writev failed errno:{} to send {} bytes", result.errno_,
                    requested_send_size);
    }

    return;
  }

  ENVOY_UDP_LOG(trace, "send requested:{} writev:{}", requested_send_size, result.rc_);
}

} // namespace Network
} // namespace Envoy
