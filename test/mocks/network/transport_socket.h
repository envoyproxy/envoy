#pragma once

#include <cstdint>
#include <list>
#include <string>
#include <vector>

#include "envoy/network/transport_socket.h"

#include "source/common/network/listen_socket_impl.h"

#include "gmock/gmock.h"

#ifdef ENVOY_ENABLE_QUIC
#include "quiche/quic/core/crypto/quic_crypto_client_config.h"
#endif

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
  MOCK_METHOD(Api::SysCallIntResult, connect, (Network::ConnectionSocket & socket));
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

class MockTransportSocketFactory : public UpstreamTransportSocketFactory {
public:
  MockTransportSocketFactory();
  ~MockTransportSocketFactory() override;

  MOCK_METHOD(bool, implementsSecureTransport, (), (const));
  MOCK_METHOD(bool, supportsAlpn, (), (const));
  MOCK_METHOD(absl::string_view, defaultServerNameIndication, (), (const));
  MOCK_METHOD(Envoy::Ssl::ClientContextSharedPtr, sslCtx, ());
  MOCK_METHOD(OptRef<const Ssl::ClientContextConfig>, clientContextConfig, (), (const));
#ifdef ENVOY_ENABLE_QUIC
  MOCK_METHOD(std::shared_ptr<quic::QuicCryptoClientConfig>, getCryptoConfig, ());
#endif

  MOCK_METHOD(TransportSocketPtr, createTransportSocket,
              (TransportSocketOptionsConstSharedPtr,
               std::shared_ptr<const Upstream::HostDescription>),
              (const));
  MOCK_METHOD(void, hashKey,
              (std::vector<uint8_t> & key, TransportSocketOptionsConstSharedPtr options), (const));
};

class MockDownstreamTransportSocketFactory : public DownstreamTransportSocketFactory {
public:
  MockDownstreamTransportSocketFactory();
  ~MockDownstreamTransportSocketFactory() override;

  MOCK_METHOD(bool, implementsSecureTransport, (), (const));
  MOCK_METHOD(TransportSocketPtr, createDownstreamTransportSocket, (), (const));
};

} // namespace Network
} // namespace Envoy
