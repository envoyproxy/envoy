#include "common/network/udp_listener_impl.h"

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

void UdpListenerImpl::setupServerSocket(const Event::DispatcherImpl& dispatcher, Socket& socket) {
  // TODO(conqerAtapple): Implementation.
  (void)dispatcher;
  (void)socket;
}

UdpListenerImpl::UdpListenerImpl(const Event::DispatcherImpl& dispatcher, Socket& socket,
                                 UdpListenerCallbacks& cb, bool bind_to_port)
    : BaseListenerImpl(dispatcher, socket, bind_to_port), cb_(cb) {}

} // namespace Network
} // namespace Envoy
