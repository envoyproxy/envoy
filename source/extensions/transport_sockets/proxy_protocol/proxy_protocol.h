#pragma once

#include "envoy/config/core/v3/proxy_protocol.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/extensions/transport_sockets/common/passthrough.h"

using envoy::config::core::v3::ProxyProtocolConfig;
using envoy::config::core::v3::ProxyProtocolConfig_Version;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace ProxyProtocol {

class UpstreamProxyProtocolSocket : public TransportSockets::PassthroughSocket,
                                    public Logger::Loggable<Logger::Id::connection> {
public:
  UpstreamProxyProtocolSocket(Network::TransportSocketPtr&& transport_socket,
                              Network::TransportSocketOptionsConstSharedPtr options,
                              ProxyProtocolConfig_Version version);

  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override;
  Network::IoResult doWrite(Buffer::Instance& buffer, bool end_stream) override;
  void onConnected() override;

private:
  void generateHeader();
  void generateHeaderV1();
  void generateHeaderV2();
  Network::IoResult writeHeader();

  Network::TransportSocketOptionsConstSharedPtr options_;
  Network::TransportSocketCallbacks* callbacks_{};
  Buffer::OwnedImpl header_buffer_{};
  ProxyProtocolConfig_Version version_{ProxyProtocolConfig_Version::ProxyProtocolConfig_Version_V1};
};

class UpstreamProxyProtocolSocketFactory : public Network::TransportSocketFactory {
public:
  UpstreamProxyProtocolSocketFactory(Network::TransportSocketFactoryPtr transport_socket_factory,
                                     ProxyProtocolConfig config);

  // Network::TransportSocketFactory
  Network::TransportSocketPtr
  createTransportSocket(Network::TransportSocketOptionsConstSharedPtr options) const override;
  bool implementsSecureTransport() const override;
  bool usesProxyProtocolOptions() const override { return true; }

private:
  Network::TransportSocketFactoryPtr transport_socket_factory_;
  ProxyProtocolConfig config_;
};

} // namespace ProxyProtocol
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
