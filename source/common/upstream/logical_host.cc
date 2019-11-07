#include "common/upstream/logical_host.h"

namespace Envoy {
namespace Upstream {

Upstream::Host::CreateConnectionData LogicalHost::createConnection(
    Event::Dispatcher& dispatcher, const Network::ConnectionSocket::OptionsSharedPtr& options,
    Network::TransportSocketOptionsSharedPtr transport_socket_options) const {
  const auto current_address = address();
  return {HostImpl::createConnection(
              dispatcher, cluster(), current_address, transportSocketFactory(), options,
              override_transport_socket_options_ != nullptr ? override_transport_socket_options_
                                                            : transport_socket_options),
          std::make_shared<RealHostDescription>(current_address, shared_from_this())};
}

} // namespace Upstream
} // namespace Envoy
