#include "common/network/base_listener_impl.h"

#include <sys/un.h>

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/event/dispatcher_impl.h"
#include "common/event/file_event_impl.h"
#include "common/network/address_impl.h"

#include "event2/listener.h"

namespace Envoy {
namespace Network {

Address::InstanceConstSharedPtr BaseListenerImpl::getLocalAddress(int fd) {
  return Address::addressFromFd(fd);
}

BaseListenerImpl::BaseListenerImpl(const Event::DispatcherImpl& dispatcher, Socket& socket,
                                   bool bind_to_port)
    : local_address_(nullptr) {
  const auto ip = socket.localAddress()->ip();

  // Only use the listen socket's local address for new connections if it is not the all hosts
  // address (e.g., 0.0.0.0 for IPv4).
  if (!(ip && ip->isAnyAddress())) {
    local_address_ = socket.localAddress();
  }

  if (bind_to_port) {
    setupServerSocket(dispatcher, socket);
  }
}

void BaseListenerImpl::setupServerSocket(const Event::DispatcherImpl& dispatcher, Socket& socket) {
  assert(false);

  (void)dispatcher;
  (void)socket;

  NOT_REACHED_GCOVR_EXCL_LINE;
}

} // namespace Network
} // namespace Envoy
