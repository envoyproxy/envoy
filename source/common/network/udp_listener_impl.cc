#include "common/network/udp_listener_impl.h"

#include <sys/un.h>

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/event/dispatcher_impl.h"
#include "common/network/address_impl.h"

#include "event2/listener.h"

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

  return ReceiveResult{Api::SysCallIntResult{static_cast<int>(result.rc_), 0}, std::move(buffer)};
}

void UdpListenerImpl::onSocketEvent(short flags) {
  ASSERT((flags & (Event::FileReadyType::Read | Event::FileReadyType::Write)));

  if (flags & Event::FileReadyType::Read) {
    handleReadCallback();
  }

  if (flags & Event::FileReadyType::Write) {
    handleWriteCallback();
  }
}

void UdpListenerImpl::handleReadCallback() {
  sockaddr_storage addr;
  socklen_t addr_len = 0;

  do {
    ReceiveResult recv_result = doRecvFrom(addr, addr_len);
    if ((recv_result.result_.rc_ < 0)) {
      if (recv_result.result_.errno_ != EAGAIN) {
        cb_.onError(UdpListenerCallbacks::ErrorCode::SyscallError, recv_result.result_.errno_);
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

    // TODO(conqerAtApple): Current implementation of Address::addressFromSockAddr
    // cannot be used here unfortunately. This should belong in Address namespace.
    switch (addr.ss_family) {
    case AF_INET: {
      const struct sockaddr_in* sin = reinterpret_cast<const struct sockaddr_in*>(&addr);
      ASSERT(AF_INET == sin->sin_family);
      peer_address = std::make_shared<Address::Ipv4Instance>(sin);

      break;
    }
    case AF_INET6: {
      const struct sockaddr_in6* sin6 = reinterpret_cast<const struct sockaddr_in6*>(&addr);
      ASSERT(AF_INET6 == sin6->sin6_family);
      if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
#if defined(__APPLE__)
        struct sockaddr_in sin = {
            {}, AF_INET, sin6->sin6_port, {sin6->sin6_addr.__u6_addr.__u6_addr32[3]}, {}};
#else
        struct sockaddr_in sin = {AF_INET, sin6->sin6_port, {sin6->sin6_addr.s6_addr32[3]}, {}};
#endif
        peer_address = std::make_shared<Address::Ipv4Instance>(&sin);
      } else {
        peer_address = std::make_shared<Address::Ipv6Instance>(*sin6, true);
      }

      break;
    }

    default:
      RELEASE_ASSERT(false,
                     fmt::format("Unsupported address family: {}, local address: {}, receive size: "
                                 "{}, address length: {}",
                                 addr.ss_family, local_address->asString(), recv_result.result_.rc_,
                                 addr_len));
      break;
    }

    RELEASE_ASSERT((peer_address != nullptr),
                   fmt::format("Unable to get remote address for fd: {}, local address: {} ",
                               socket_.ioHandle().fd(), local_address->asString()));

    RELEASE_ASSERT((local_address != nullptr),
                   fmt::format("Unable to get local address for fd: {}", socket_.ioHandle().fd()));

    cb_.onData(UdpData{local_address, peer_address, std::move(recv_result.buffer_)});

  } while (true);
}

void UdpListenerImpl::handleWriteCallback() { cb_.onWriteReady(socket_); }

} // namespace Network
} // namespace Envoy
