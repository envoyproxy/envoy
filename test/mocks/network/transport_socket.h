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
  MOCK_METHOD(void, setTransportSocketCallbacks, (TransportSocketCallbacks & callbacks),
              (override));
  MOCK_METHOD(std::string, protocol, (), (const, override));
  MOCK_METHOD(absl::string_view, failureReason, (), (const, override));
  MOCK_METHOD(bool, canFlushClose, (), (override));
  MOCK_METHOD(Api::SysCallIntResult, connect, (Network::ConnectionSocket & socket), (override));
  MOCK_METHOD(void, closeSocket, (Network::ConnectionEvent event, bool abort_reset), (override));
  MOCK_METHOD(IoResult, doRead, (Buffer::Instance & buffer), (override));
  MOCK_METHOD(IoResult, doWrite, (Buffer::Instance & buffer, bool end_stream), (override));
  MOCK_METHOD(void, onConnected, (), (override));
  MOCK_METHOD(Ssl::ConnectionInfoConstSharedPtr, ssl, (), (const, override));
  MOCK_METHOD(bool, startSecureTransport, (), (override));
  MOCK_METHOD(void, configureInitialCongestionWindow,
              (uint64_t bandwidth_bits_per_sec, std::chrono::microseconds rtt), (override));

  TransportSocketCallbacks* callbacks_{};
};

class MockTransportSocketFactory : public UpstreamTransportSocketFactory {
public:
  MockTransportSocketFactory();
  ~MockTransportSocketFactory() override;

  MOCK_METHOD(bool, implementsSecureTransport, (), (const, override));
  MOCK_METHOD(bool, supportsAlpn, (), (const, override));
  MOCK_METHOD(absl::string_view, defaultServerNameIndication, (), (const, override));
  MOCK_METHOD(Envoy::Ssl::ClientContextSharedPtr, sslCtx, (), (override));
  MOCK_METHOD(OptRef<const Ssl::ClientContextConfig>, clientContextConfig, (), (const, override));
#ifdef ENVOY_ENABLE_QUIC
  MOCK_METHOD(std::shared_ptr<quic::QuicCryptoClientConfig>, getCryptoConfig, (), (override));
#endif

  MOCK_METHOD(TransportSocketPtr, createTransportSocket,
              (TransportSocketOptionsConstSharedPtr,
               std::shared_ptr<const Upstream::HostDescription>),
              (const, override));
  MOCK_METHOD(void, hashKey,
              (std::vector<uint8_t> & key, TransportSocketOptionsConstSharedPtr options),
              (const, override));
};

class MockDownstreamTransportSocketFactory : public DownstreamTransportSocketFactory {
public:
  MockDownstreamTransportSocketFactory();
  ~MockDownstreamTransportSocketFactory() override;

  MOCK_METHOD(bool, implementsSecureTransport, (), (const, override));
  MOCK_METHOD(TransportSocketPtr, createDownstreamTransportSocket, (), (const, override));
};

} // namespace Network
} // namespace Envoy
