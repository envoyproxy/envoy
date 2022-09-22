#pragma once

#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/transport_socket_options_impl.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {

class PassthroughFactory : public Network::CommonUpstreamTransportSocketFactory {
public:
  PassthroughFactory(Network::UpstreamTransportSocketFactoryPtr&& transport_socket_factory)
      : transport_socket_factory_(std::move(transport_socket_factory)) {
    ASSERT(transport_socket_factory_ != nullptr);
  }

  bool implementsSecureTransport() const override {
    return transport_socket_factory_->implementsSecureTransport();
  }
  bool supportsAlpn() const override { return transport_socket_factory_->supportsAlpn(); }
  absl::string_view defaultServerNameIndication() const override {
    return transport_socket_factory_->defaultServerNameIndication();
  }
  void hashKey(std::vector<uint8_t>& key,
               Network::TransportSocketOptionsConstSharedPtr options) const override {
    if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.fix_hash_key")) {
      return transport_socket_factory_->hashKey(key, options);
    }
    return Network::CommonUpstreamTransportSocketFactory::hashKey(key, options);
  }
  Envoy::Ssl::ClientContextSharedPtr sslCtx() override {
    return transport_socket_factory_->sslCtx();
  }
  OptRef<const Ssl::ClientContextConfig> clientContextConfig() const override {
    return transport_socket_factory_->clientContextConfig();
  }
  std::shared_ptr<quic::QuicCryptoClientConfig> getCryptoConfig() override {
    return transport_socket_factory_->getCryptoConfig();
  }

protected:
  // The wrapped factory.
  Network::UpstreamTransportSocketFactoryPtr transport_socket_factory_;
};

class DownstreamPassthroughFactory : public Network::DownstreamTransportSocketFactory {
public:
  DownstreamPassthroughFactory(
      Network::DownstreamTransportSocketFactoryPtr&& transport_socket_factory)
      : transport_socket_factory_(std::move(transport_socket_factory)) {
    ASSERT(transport_socket_factory_ != nullptr);
  }

  bool implementsSecureTransport() const override {
    return transport_socket_factory_->implementsSecureTransport();
  }

protected:
  // The wrapped factory.
  Network::DownstreamTransportSocketFactoryPtr transport_socket_factory_;
};

class PassthroughSocket : public Network::TransportSocket {
public:
  PassthroughSocket(Network::TransportSocketPtr&& transport_socket);

  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override;
  std::string protocol() const override;
  absl::string_view failureReason() const override;
  bool canFlushClose() override;
  Api::SysCallIntResult connect(Network::ConnectionSocket& socket) override;
  void closeSocket(Network::ConnectionEvent event) override;
  Network::IoResult doRead(Buffer::Instance& buffer) override;
  Network::IoResult doWrite(Buffer::Instance& buffer, bool end_stream) override;
  void onConnected() override;
  Ssl::ConnectionInfoConstSharedPtr ssl() const override;
  // startSecureTransport method should not be called for this transport socket.
  bool startSecureTransport() override { return false; }
  void configureInitialCongestionWindow(uint64_t bandwidth_bits_per_sec,
                                        std::chrono::microseconds rtt) override;

protected:
  Network::TransportSocketPtr transport_socket_;
};

} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
