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

Buffer::InstancePtr UdpListenerImpl::getBufferImpl() {
  return std::make_unique<Buffer::OwnedImpl>();
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

  // TODO(conqerAtAppple): Make this configurable or get from system.
  constexpr uint64_t const read_length = 16384;
  Buffer::InstancePtr buffer = getBufferImpl();
  ASSERT(buffer);

  sockaddr_storage addr;
  socklen_t addr_len;
  Api::SysCallIntResult result;

  do {
    result = buffer->recvFrom(fd, read_length, addr, addr_len);
    if (result.rc_ < 0) {
      if (result.rc_ == -EAGAIN) {
        continue;
      }
      // TODO(conqerAtApple): Call error callback.
      cb_.onError(UdpListenerCallbacks::ErrorCode::SYSCALL_ERROR, result.errno_);
      return;
    }

    break;
  } while (true);

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
                               addr.ss_family, local_address->asString(), result.rc_, addr_len));
    break;
  }

  RELEASE_ASSERT((peer_address != nullptr),
                 fmt::format("Unable to get remote address for fd: {}, local address: {} ", fd,
                             local_address->asString()));

  RELEASE_ASSERT((local_address != nullptr),
                 fmt::format("Unable to get local address for fd: {}", fd));

  bool expected = true;
  if (is_first_.compare_exchange_strong(expected, false)) {
    cb_.onNewConnection(local_address, peer_address, std::move(buffer));
  } else {
    cb_.onData(local_address, peer_address, std::move(buffer));
  }
}

void UdpListenerImpl::handleWriteCallback(int fd) {
  RELEASE_ASSERT(fd == socket_.fd(),
                 fmt::format("Invalid socket descriptor received in callback {}", fd));

  cb_.onWriteReady(socket_);
}

} // namespace Network
} // namespace Envoy
