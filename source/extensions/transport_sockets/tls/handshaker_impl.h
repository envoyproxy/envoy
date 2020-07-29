#pragma once

#include "envoy/ssl/handshaker.h"
#include "envoy/ssl/socket_state.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

// Class to abstract handshaking behavior.
// Manages translation between SSL error codes and Network::PostIoAction
// response enums, among other things.
class HandshakerImpl : public Envoy::Ssl::Handshaker {
public:
  HandshakerImpl(bssl::UniquePtr<SSL> ssl) : ssl_(std::move(ssl)) {}

  Network::PostIoAction doHandshake(Envoy::Ssl::SocketState& state,
                                    Ssl::HandshakerCallbacks& callbacks) override;

  void setTransportSocketCallbacks(Network::TransportSocketCallbacks& callbacks) override {
    transport_socket_callbacks_ = &callbacks;
  }

  SSL* ssl() override { return ssl_.get(); }

private:
  bssl::UniquePtr<SSL> ssl_;
  Network::TransportSocketCallbacks* transport_socket_callbacks_{};
};

class HandshakerFactoryContextImpl : public Ssl::HandshakerFactoryContext {
public:
  HandshakerFactoryContextImpl(Api::Api& api, absl::string_view alpn_protocols)
      : api_(api), alpn_protocols_(alpn_protocols) {}

  // HandshakerFactoryContext
  Api::Api& api() override { return api_; }
  absl::string_view alpnProtocols() const override { return alpn_protocols_; }

private:
  Api::Api& api_;
  const std::string alpn_protocols_;
};

class HandshakerFactoryImpl : public Ssl::HandshakerFactory {
public:
  std::string name() const override { return "envoy.default_tls_handshaker"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }

  Ssl::HandshakerFactoryCb createHandshakerCb(const Protobuf::Message&,
                                              Ssl::HandshakerFactoryContext&,
                                              ProtobufMessage::ValidationVisitor&) override {
    // The default HandshakerImpl doesn't take a config or use the HandshakerFactoryContext.
    return
        [](bssl::UniquePtr<SSL> ssl) { return std::make_shared<HandshakerImpl>(std::move(ssl)); };
  }

  bool requireCertificates() const override {
    // The default HandshakerImpl does require certificates.
    return true;
  }

  static HandshakerFactory* getDefaultHandshakerFactory() {
    static HandshakerFactoryImpl default_handshaker_factory;
    return &default_handshaker_factory;
  }
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
