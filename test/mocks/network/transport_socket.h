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

  MOCK_METHOD(bool, implementsSecureTransport, (), (const));
  MOCK_METHOD(void, setTransportSocketCallbacks, (TransportSocketCallbacks & callbacks));
  MOCK_METHOD(std::string, protocol, (), (const));
  MOCK_METHOD(absl::string_view, failureReason, (), (const));
  MOCK_METHOD(bool, canFlushClose, ());
  MOCK_METHOD(void, closeSocket, (Network::ConnectionEvent event));
  MOCK_METHOD(IoResult, doRead, (Buffer::Instance & buffer));
  MOCK_METHOD(IoResult, doWrite, (Buffer::Instance & buffer, bool end_stream));
  MOCK_METHOD(void, onConnected, ());
  MOCK_METHOD(Ssl::ConnectionInfoConstSharedPtr, ssl, (), (const));
  MOCK_METHOD(bool, startSecureTransport, ());
  MOCK_METHOD(void, configureInitialCongestionWindow,
              (uint64_t bandwidth_bits_per_sec, std::chrono::microseconds rtt));

  TransportSocketCallbacks* callbacks_{};
};

class MockTransportSocketFactory : public TransportSocketFactory {
public:
  MockTransportSocketFactory();
  ~MockTransportSocketFactory() override;

  MOCK_METHOD(bool, implementsSecureTransport, (), (const));
  MOCK_METHOD(bool, supportsAlpn, (), (const));
  MOCK_METHOD(TransportSocketPtr, createTransportSocket, (TransportSocketOptionsConstSharedPtr),
              (const));
  MOCK_METHOD(void, hashKey,
              (std::vector<uint8_t> & key, TransportSocketOptionsConstSharedPtr options), (const));
};

} // namespace Network
} // namespace Envoy
