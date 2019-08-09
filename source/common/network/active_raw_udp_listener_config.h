#include "envoy/registry/registry.h"

#include "server/active_udp_listener_config.h"
#include "server/connection_handler_impl.h"

namespace Envoy {
namespace Network {

// This class uses a protobuf config to create a UDP listener factory which
// creates a Server::ConnectionHandlerImpl::ActiveUdpListener.
// This is the default UDP listener if not specified in config.
class ActiveRawUdpListenerConfigFactory : public Server::ActiveUdpListenerConfigFactory {

  std::unique_ptr<Server::ActiveUdpListenerFactory>
  createActiveUdpListenerFactory(const Protobuf::Message&) override;

  std::string name() override;
};

DECLARE_FACTORY(ActiveRawUdpListenerConfigFactory);

} // namespace Network
} // namespace Envoy
