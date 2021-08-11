#include "source/server/active_raw_udp_listener_config.h"

#include <memory>
#include <string>

#include "source/server/active_udp_listener.h"
#include "source/server/connection_handler_impl.h"

namespace Envoy {
namespace Server {

ActiveRawUdpListenerFactory::ActiveRawUdpListenerFactory(uint32_t concurrency)
    : concurrency_(concurrency) {}

Network::ConnectionHandler::ActiveUdpListenerPtr
ActiveRawUdpListenerFactory::createActiveUdpListener(uint32_t worker_index,
                                                     Network::UdpConnectionHandler& parent,
                                                     Event::Dispatcher& dispatcher,
                                                     Network::ListenerConfig& config) {
  return std::make_unique<ActiveRawUdpListener>(worker_index, concurrency_, parent, dispatcher,
                                                config);
}

} // namespace Server
} // namespace Envoy
