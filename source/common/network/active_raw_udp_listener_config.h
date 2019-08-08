#include "envoy/registry/registry.h"

#include "server/active_udp_listener_config.h"
#include "server/connection_handler_impl.h"

namespace Envoy {
namespace Network {

class ActiveRawUdpListenerConfigFactory : public Server::ActiveUdpListenerConfigFactory {

  std::unique_ptr<Server::ActiveUdpListenerFactory>
  createActiveUdpListenerFactory(const Protobuf::Message&) override;

  std::string name() override;
};

DECLARE_FACTORY(ActiveRawUdpListenerConfigFactory);

} // namespace Network
} // namespace Envoy
