#include "source/extensions/clusters/common/logical_host.h"

namespace Envoy {
namespace Upstream {

Upstream::Host::CreateConnectionData LogicalHost::createConnection(
    Event::Dispatcher& dispatcher, const Network::ConnectionSocket::OptionsSharedPtr& options,
    Network::TransportSocketOptionsConstSharedPtr transport_socket_options) const {
  const std::pair<Network::Address::InstanceConstSharedPtr,
                  const std::vector<Network::Address::InstanceConstSharedPtr>>
      address = addressAndListCopy();
  return HostImplBase::createConnection(
      dispatcher, cluster(), address.first, address.second, transportSocketFactory(), options,
      override_transport_socket_options_ != nullptr ? override_transport_socket_options_
                                                    : transport_socket_options,
      std::make_shared<RealHostDescription>(address.first, shared_from_this()));
}

} // namespace Upstream
} // namespace Envoy
