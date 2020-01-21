#pragma once

#include "envoy/event/timer.h"
#include "envoy/extensions/transport_sockets/tap/v3/tap.pb.h"
#include "envoy/network/transport_socket.h"

#include "extensions/common/tap/extension_config_base.h"
#include "extensions/transport_sockets/tap/tap_config.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tap {

class TapSocket : public Network::TransportSocket {
public:
  TapSocket(SocketTapConfigSharedPtr config, Network::TransportSocketPtr&& transport_socket);

  // Network::TransportSocket
  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override;
  std::string protocol() const override;
  absl::string_view failureReason() const override;
  bool canFlushClose() override;
  void closeSocket(Network::ConnectionEvent event) override;
  Network::IoResult doRead(Buffer::Instance& buffer) override;
  Network::IoResult doWrite(Buffer::Instance& buffer, bool end_stream) override;
  void onConnected() override;
  Ssl::ConnectionInfoConstSharedPtr ssl() const override;

private:
  SocketTapConfigSharedPtr config_;
  PerSocketTapperPtr tapper_;
  Network::TransportSocketPtr transport_socket_;
};

class TapSocketFactory : public Network::TransportSocketFactory,
                         public Common::Tap::ExtensionConfigBase {
public:
  TapSocketFactory(const envoy::extensions::transport_sockets::tap::v3::Tap& proto_config,
                   Common::Tap::TapConfigFactoryPtr&& config_factory, Server::Admin& admin,
                   Singleton::Manager& singleton_manager, ThreadLocal::SlotAllocator& tls,
                   Event::Dispatcher& main_thread_dispatcher,
                   Network::TransportSocketFactoryPtr&& transport_socket_factory);

  // Network::TransportSocketFactory
  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsSharedPtr options) const override;
  bool implementsSecureTransport() const override;

private:
  Network::TransportSocketFactoryPtr transport_socket_factory_;
};

} // namespace Tap
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
