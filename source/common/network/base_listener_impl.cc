#include "source/common/network/base_listener_impl.h"

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/fmt.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/event/file_event_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/socket_impl.h"

#include "event2/listener.h"

namespace Envoy {
namespace Network {

BaseListenerImpl::BaseListenerImpl(Event::Dispatcher& dispatcher, SocketSharedPtr socket)
    : local_address_(nullptr), dispatcher_(dispatcher), socket_(std::move(socket)) {
  const auto ip = socket_->connectionInfoProvider().localAddress()->ip();

  // Only use the listen socket's local address for new connections if it is not the all hosts
  // address (e.g., 0.0.0.0 for IPv4).
  if (!(ip && ip->isAnyAddress())) {
    local_address_ = socket_->connectionInfoProvider().localAddress();
  }
}

} // namespace Network
} // namespace Envoy
