#pragma once

#include "envoy/config/core/v3/proxy_protocol.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/logger.h"

using envoy::config::core::v3::ProxyProtocolConfig_Version;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace ProxyProtocol {

class ProxyProtocolSocket : public Network::TransportSocket,
                            public Logger::Loggable<Logger::Id::connection> {
public:
  ProxyProtocolSocket(Network::TransportSocketPtr transport_socket,
                      Network::TransportSocketOptionsSharedPtr options,
                      ProxyProtocolConfig_Version version);

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
  void generateHeader();
  void generateHeaderV1();
  void generateHeaderV2();
  Network::IoResult writeHeader();

  Network::TransportSocketPtr transport_socket_;
  Network::TransportSocketOptionsSharedPtr options_;
  Network::TransportSocketCallbacks* callbacks_{};
  Buffer::OwnedImpl header_buffer_{};
  ProxyProtocolConfig_Version version_{ProxyProtocolConfig_Version::ProxyProtocolConfig_Version_V1};
};

} // namespace ProxyProtocol
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy