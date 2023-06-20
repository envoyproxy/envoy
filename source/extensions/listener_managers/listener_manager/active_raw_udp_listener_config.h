#pragma once

#include "envoy/network/connection_handler.h"

namespace Envoy {
namespace Server {

class ActiveRawUdpListenerFactory : public Network::ActiveUdpListenerFactory {
public:
  ActiveRawUdpListenerFactory(uint32_t concurrency);

  Network::ConnectionHandler::ActiveUdpListenerPtr
  createActiveUdpListener(Runtime::Loader&, uint32_t worker_index,
                          Network::UdpConnectionHandler& parent,
                          Network::SocketSharedPtr&& listen_socket_ptr,
                          Event::Dispatcher& disptacher, Network::ListenerConfig& config) override;
  bool isTransportConnectionless() const override { return true; }
  const Network::Socket::OptionsSharedPtr& socketOptions() const override { return options_; }

private:
  const uint32_t concurrency_;
  const Network::Socket::OptionsSharedPtr options_{std::make_shared<Network::Socket::Options>()};
};

} // namespace Server
} // namespace Envoy
