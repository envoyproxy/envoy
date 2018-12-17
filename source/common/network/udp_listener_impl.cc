#include "common/network/udp_listener_impl.h"

#include <sys/un.h>

#include <cassert>

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/event/dispatcher_impl.h"
#include "common/network/address_impl.h"

#include "event2/listener.h"

namespace Envoy {
namespace Network {

UdpListenerImpl::UdpListenerImpl(const Event::DispatcherImpl& dispatcher, Socket& socket,
                                 UdpListenerCallbacks& cb, bool bind_to_port)
    : BaseListenerImpl(dispatcher, socket), cb_(cb) {
  if (bind_to_port) {
    event_assign(&raw_event_, &dispatcher.base(), socket.fd(), EV_READ | EV_PERSIST, readCallback,
                 this);
    event_add(&raw_event_, nullptr);

    if (!Network::Socket::applyOptions(socket.options(), socket,
                                       envoy::api::v2::core::SocketOption::STATE_BOUND)) {
      throw CreateListenerException(fmt::format("cannot set post-bound socket option on socket: {}",
                                                socket.localAddress()->asString()));
    }
  }
}

void UdpListenerImpl::disable() { event_del(&raw_event_); }

void UdpListenerImpl::enable() { event_add(&raw_event_, nullptr); }

void UdpListenerImpl::readCallback(int fd, short flags, void* arg) {
  (void)flags;

  UdpListenerImpl* instance = static_cast<UdpListenerImpl*>(arg);
  // TODO(conqerAtApple): Replace with envoy style assert

  assert(instance);
  // TODO(conqerAtAppple): Make this configurable or get from system.
  constexpr uint64_t const read_length = 16384;
  Buffer::OwnedImpl buffer;

  Api::SysCallIntResult result = buffer.read(fd, read_length);
  if (result.rc_ < 0) {
    // TODO(conqerAtApple): Call error callback.
  }

  Address::InstanceConstSharedPtr local_address = instance->socket_.localAddress();
  RELEASE_ASSERT(local_address, fmt::format("Unable to get local address for fd: {}", fd));

  Address::InstanceConstSharedPtr remote_address = Address::peerAddressFromFd(fd);

  RELEASE_ASSERT(remote_address,
                 fmt::format("Unable to get remote address for fd: {}, local address: {} ", fd,
                             local_address->asString()));

  instance->cb_.onNewConnection(local_address, remote_address, std::move(buffer));
}

} // namespace Network
} // namespace Envoy
