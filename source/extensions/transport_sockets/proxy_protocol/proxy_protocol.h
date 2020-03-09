#pragma once

#include "envoy/extensions/transport_sockets/proxy_protocol/v3/proxy_protocol.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"

#include "common/common/logger.h"

using envoy::extensions::transport_sockets::proxy_protocol::v3::ProxyProtocol_Version;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace ProxyProtocol {

class ProxyProtocolSocket : public Network::TransportSocket,
                            public Logger::Loggable<Logger::Id::connection> {
public:
  ProxyProtocolSocket(Network::TransportSocketPtr transport_socket,
                      Network::TransportSocketOptionsSharedPtr options,
                      ProxyProtocol_Version version);

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
  Network::IoResult injectHeader();
  Network::IoResult injectHeaderV1();
  Network::IoResult injectHeaderV2();

  Network::TransportSocketPtr transport_socket_;
  Network::TransportSocketOptionsSharedPtr options_;
  Network::TransportSocketCallbacks* callbacks_{};
  bool injected_header_{false};
  ProxyProtocol_Version version_{ProxyProtocol_Version::ProxyProtocol_Version_V1};
};

class ProxyProtocolSocketFactory : public Network::TransportSocketFactory {
public:
  ProxyProtocolSocketFactory(Network::TransportSocketFactoryPtr transport_socket_factory,
                             ProxyProtocol_Version version);

  // Network::TransportSocketFactory
  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsSharedPtr options) const override;
  bool implementsSecureTransport() const override;

private:
  Network::TransportSocketFactoryPtr transport_socket_factory_;
  ProxyProtocol_Version version_{ProxyProtocol_Version::ProxyProtocol_Version_V1};
};

} // namespace ProxyProtocol
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
