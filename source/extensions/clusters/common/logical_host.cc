#include "source/extensions/clusters/common/logical_host.h"

namespace Envoy {
namespace Upstream {

Upstream::Host::CreateConnectionData LogicalHost::createConnection(
    Event::Dispatcher& dispatcher, const Network::ConnectionSocket::OptionsSharedPtr& options,
    Network::TransportSocketOptionsConstSharedPtr transport_socket_options) const {
  const auto& [address, shared_address_list] = addressAndListCopy();
  return HostImplBase::createConnection(
      dispatcher, cluster(), address, *shared_address_list, transportSocketFactory(), options,
      override_transport_socket_options_ != nullptr ? override_transport_socket_options_
                                                    : transport_socket_options,
      std::make_shared<RealHostDescription>(address, shared_from_this()));
}

} // namespace Upstream
} // namespace Envoy
