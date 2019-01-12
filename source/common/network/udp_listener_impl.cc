#include "common/network/udp_listener_impl.h"

#include <sys/un.h>

#include <cassert>

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/event/dispatcher_impl.h"
#include "common/network/address_impl.h"

#include "event2/listener.h"

namespace Envoy {
namespace Network {

UdpListenerImpl::UdpListenerImpl(const Event::DispatcherImpl& dispatcher, Socket& socket,
                                 UdpListenerCallbacks& cb)
    : BaseListenerImpl(dispatcher, socket), cb_(cb), is_first_(true) {
  event_assign(&raw_event_, &dispatcher.base(), socket.fd(), EV_READ | EV_WRITE | EV_PERSIST,
               eventCallback, this);
  event_add(&raw_event_, nullptr);

  if (!Network::Socket::applyOptions(socket.options(), socket,
                                     envoy::api::v2::core::SocketOption::STATE_BOUND)) {
    throw CreateListenerException(fmt::format("cannot set post-bound socket option on socket: {}",
                                              socket.localAddress()->asString()));
  }
}

void UdpListenerImpl::disable() { event_del(&raw_event_); }

void UdpListenerImpl::enable() { event_add(&raw_event_, nullptr); }

UdpListenerImpl::ReceiveResult UdpListenerImpl::doRecvFrom(sockaddr_storage& peer_addr,
                                                           socklen_t& addr_len) {
  constexpr uint64_t const read_length = 16384;

  Buffer::InstancePtr buffer = std::make_unique<Buffer::OwnedImpl>();

  addr_len = sizeof(sockaddr_storage);
  memset(&peer_addr, 0, addr_len);

  Buffer::RawSlice slice;
  const uint64_t num_slices = buffer->reserve(read_length, &slice, 1);

  ASSERT(num_slices == 1);
  // TODO(conqerAtapple): Use os_syscalls
  const ssize_t rc = ::recvfrom(socket_.fd(), slice.mem_, read_length, 0,
                                reinterpret_cast<struct sockaddr*>(&peer_addr), &addr_len);
  if (rc < 0) {
    return ReceiveResult{Api::SysCallIntResult{static_cast<int>(rc), errno}, nullptr};
  }

  slice.len_ = std::min(slice.len_, static_cast<size_t>(rc));
  buffer->commit(&slice, 1);

  return ReceiveResult{Api::SysCallIntResult{static_cast<int>(rc), 0}, std::move(buffer)};
}

void UdpListenerImpl::eventCallback(int fd, short flags, void* arg) {
  RELEASE_ASSERT((flags & (EV_READ | EV_WRITE)),
                 fmt::format("Unexpected flags for callback: {}", flags));

  UdpListenerImpl* instance = static_cast<UdpListenerImpl*>(arg);
  ASSERT(instance);

  if (flags & EV_READ) {
    instance->handleReadCallback(fd);
  }

  if (flags & EV_WRITE) {
    instance->handleWriteCallback(fd);
  }
}

void UdpListenerImpl::handleReadCallback(int fd) {
  RELEASE_ASSERT(fd == socket_.fd(),
                 fmt::format("Invalid socket descriptor received in callback {}", fd));

  sockaddr_storage addr;
  socklen_t addr_len;
  ReceiveResult recv_result;

  do {
    recv_result = doRecvFrom(addr, addr_len);
    if ((recv_result.result_.rc_ < 0) && (recv_result.result_.rc_ != -EAGAIN)) {
      cb_.onError(UdpListenerCallbacks::ErrorCode::SYSCALL_ERROR, recv_result.result_.errno_);
      return;
    }

  } while (recv_result.result_.rc_ == -EAGAIN);

  Address::InstanceConstSharedPtr local_address = socket_.localAddress();

  RELEASE_ASSERT(
      addr_len > 0,
      fmt::format(
          "Unable to get remote address for fd: {}, local address: {}. address length is 0 ", fd,
          local_address->asString()));

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
                 fmt::format("Unable to get remote address for fd: {}, local address: {} ", fd,
                             local_address->asString()));

  RELEASE_ASSERT((local_address != nullptr),
                 fmt::format("Unable to get local address for fd: {}", fd));

  bool expected = true;
  if (is_first_.compare_exchange_strong(expected, false)) {
    cb_.onNewConnection(local_address, peer_address, std::move(recv_result.buffer_));
  } else {
    cb_.onData(local_address, peer_address, std::move(recv_result.buffer_));
  }
}

void UdpListenerImpl::handleWriteCallback(int fd) {
  RELEASE_ASSERT(fd == socket_.fd(),
                 fmt::format("Invalid socket descriptor received in callback {}", fd));

  cb_.onWriteReady(socket_);
}

} // namespace Network
} // namespace Envoy
