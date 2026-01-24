#include "source/server/config_validation/dispatcher.h"

#include <memory>
#include <utility>

#include "source/common/common/assert.h"
#include "source/common/network/connection_socket_impl.h"
#include "source/server/config_validation/connection.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Event {

Network::ClientConnectionPtr ValidationDispatcher::createClientConnection(
    Network::Address::InstanceConstSharedPtr remote_address,
    Network::Address::InstanceConstSharedPtr source_address,
    Network::TransportSocketPtr&& transport_socket,
    const Network::ConnectionSocket::OptionsSharedPtr& options,
    const Network::TransportSocketOptionsConstSharedPtr& transport_options) {
  absl::StatusOr<std::unique_ptr<Network::ClientSocketImpl>> socket_or =
      Network::ClientSocketImpl::create(remote_address, options);
  RELEASE_ASSERT(socket_or.ok(), absl::StrCat("failed to create socket: ", socket_or.status()));

  return std::make_unique<Network::ConfigValidateConnection>(
      *this, std::move(*socket_or), source_address, std::move(transport_socket), options,
      transport_options);
}

} // namespace Event
} // namespace Envoy
