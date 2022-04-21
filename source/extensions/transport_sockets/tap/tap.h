#pragma once

#include "envoy/event/timer.h"
#include "envoy/extensions/transport_sockets/tap/v3/tap.pb.h"
#include "envoy/network/transport_socket.h"

#include "source/extensions/common/tap/extension_config_base.h"
#include "source/extensions/transport_sockets/common/passthrough.h"
#include "source/extensions/transport_sockets/tap/tap_config.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tap {

class TapSocket : public TransportSockets::PassthroughSocket {
public:
  TapSocket(SocketTapConfigSharedPtr config, Network::TransportSocketPtr&& transport_socket);

  // Network::TransportSocket
  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override;
  void closeSocket(Network::ConnectionEvent event) override;
  Network::IoResult doRead(Buffer::Instance& buffer) override;
  Network::IoResult doWrite(Buffer::Instance& buffer, bool end_stream) override;

private:
  SocketTapConfigSharedPtr config_;
  PerSocketTapperPtr tapper_;
};

class TapSocketFactory : public Common::Tap::ExtensionConfigBase, public PassthroughFactory {
public:
  TapSocketFactory(const envoy::extensions::transport_sockets::tap::v3::Tap& proto_config,
                   Common::Tap::TapConfigFactoryPtr&& config_factory, Server::Admin& admin,
                   Singleton::Manager& singleton_manager, ThreadLocal::SlotAllocator& tls,
                   Event::Dispatcher& main_thread_dispatcher,
                   Network::TransportSocketFactoryPtr&& transport_socket_factory);

  // Network::TransportSocketFactory
  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsConstSharedPtr options) const override;
};

} // namespace Tap
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
