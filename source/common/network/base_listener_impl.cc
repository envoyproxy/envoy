#include "source/common/network/base_listener_impl.h"

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/fmt.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/event/file_event_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/socket_impl.h"
#include "source/common/runtime/runtime_features.h"

#include "event2/listener.h"

namespace Envoy {
namespace Network {

BaseListenerImpl::BaseListenerImpl(Event::DispatcherImpl& dispatcher, SocketSharedPtr socket)
    : local_address_(nullptr), dispatcher_(dispatcher), socket_(std::move(socket)) {
  const auto ip = socket_->connectionInfoProvider().localAddress()->ip();

  // Only use the listen socket's local address for new connections if it is not the all hosts
  // address (e.g., 0.0.0.0 for IPv4).
  if (!(ip && ip->isAnyAddress())) {
    // Always treat the IPv4-mapped local address as IPv4 address, so convert it
    // to IPv4 address here.
    if (Runtime::runtimeFeatureEnabled(
            "envoy.reloadable_features.convert_ipv4_mapped_address_to_ipv4_address") &&
        ip != nullptr && ip->version() == Address::IpVersion::v6 && !ip->ipv6()->v6only()) {
      local_address_ = ip->ipv6()->v4CompatibleAddress();
      ASSERT(local_address_ != nullptr);
    } else {
      local_address_ = socket_->connectionInfoProvider().localAddress();
    }
  }
}

} // namespace Network
} // namespace Envoy
