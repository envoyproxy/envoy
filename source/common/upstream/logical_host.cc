#include "source/common/upstream/logical_host.h"

namespace Envoy {
namespace Upstream {

Upstream::Host::CreateConnectionData LogicalHost::createConnection(
    Event::Dispatcher& dispatcher, const Network::ConnectionSocket::OptionsSharedPtr& options,
    Network::TransportSocketOptionsConstSharedPtr transport_socket_options) const {
  HostDescriptionConstSharedPtr host = std::make_shared<RealHostDescription>(address(), shared_from_this());
  return {HostImpl::createConnection(dispatcher, host,
                                     transportSocketFactory(), options,
                                     override_transport_socket_options_ != nullptr
                                         ? override_transport_socket_options_
                                         : transport_socket_options, false),
          host};
}

} // namespace Upstream
} // namespace Envoy
