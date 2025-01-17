#include "source/common/listener_manager/active_raw_udp_listener_config.h"

#include <memory>
#include <string>

#include "source/common/listener_manager/connection_handler_impl.h"
#include "source/server/active_udp_listener.h"

namespace Envoy {
namespace Server {

ActiveRawUdpListenerFactory::ActiveRawUdpListenerFactory(uint32_t concurrency)
    : concurrency_(concurrency) {}

Network::ConnectionHandler::ActiveUdpListenerPtr
ActiveRawUdpListenerFactory::createActiveUdpListener(Runtime::Loader&, uint32_t worker_index,
                                                     Network::UdpConnectionHandler& parent,
                                                     Network::SocketSharedPtr&& listen_socket_ptr,
                                                     Event::Dispatcher& dispatcher,
                                                     Network::ListenerConfig& config) {
  return std::make_unique<ActiveRawUdpListener>(worker_index, concurrency_, parent,
                                                std::move(listen_socket_ptr), dispatcher, config);
}

} // namespace Server
} // namespace Envoy
