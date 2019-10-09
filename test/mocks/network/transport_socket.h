#pragma once

#include <cstdint>
#include <list>
#include <string>
#include <vector>

#include "envoy/network/transport_socket.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Network {
class MockTransportSocket : public TransportSocket {
public:
  MockTransportSocket();
  ~MockTransportSocket() override;

  MOCK_CONST_METHOD0(implementsSecureTransport, bool());
  MOCK_METHOD1(setTransportSocketCallbacks, void(TransportSocketCallbacks& callbacks));
  MOCK_CONST_METHOD0(protocol, std::string());
  MOCK_CONST_METHOD0(failureReason, absl::string_view());
  MOCK_METHOD0(canFlushClose, bool());
  MOCK_METHOD1(closeSocket, void(Network::ConnectionEvent event));
  MOCK_METHOD1(doRead, IoResult(Buffer::Instance& buffer));
  MOCK_METHOD2(doWrite, IoResult(Buffer::Instance& buffer, bool end_stream));
  MOCK_METHOD0(onConnected, void());
  MOCK_CONST_METHOD0(ssl, Ssl::ConnectionInfoConstSharedPtr());

  TransportSocketCallbacks* callbacks_{};
};

class MockTransportSocketFactory : public TransportSocketFactory {
public:
  MockTransportSocketFactory();
  ~MockTransportSocketFactory() override;

  MOCK_CONST_METHOD0(implementsSecureTransport, bool());
  MOCK_CONST_METHOD1(createTransportSocket, TransportSocketPtr(TransportSocketOptionsSharedPtr));
};

} // namespace Network
} // namespace Envoy