#pragma once

#include <cstdint>

#include "envoy/network/connection.h"
#include "envoy/network/transport_socket.h"
#include "envoy/secret/secret_callbacks.h"
#include "envoy/server/options.h"
#include "envoy/ssl/handshaker.h"
#include "envoy/ssl/private_key/private_key_callbacks.h"
#include "envoy/ssl/ssl_socket_extended_info.h"
#include "envoy/ssl/ssl_socket_state.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/extensions/transport_sockets/tls/connection_info_impl_base.h"
#include "source/extensions/transport_sockets/tls/utility.h"

#include "absl/container/node_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/optional.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class SslExtendedSocketInfoImpl : public Envoy::Ssl::SslExtendedSocketInfo {
public:
  void setCertificateValidationStatus(Envoy::Ssl::ClientValidationStatus validated) override;
  Envoy::Ssl::ClientValidationStatus certificateValidationStatus() const override;

private:
  Envoy::Ssl::ClientValidationStatus certificate_validation_status_{
      Envoy::Ssl::ClientValidationStatus::NotValidated};
};

class SslHandshakerImpl : public ConnectionInfoImplBase,
                          public Ssl::Handshaker,
                          protected Logger::Loggable<Logger::Id::connection> {
public:
  SslHandshakerImpl(bssl::UniquePtr<SSL> ssl, int ssl_extended_socket_info_index,
                    Ssl::HandshakeCallbacks* handshake_callbacks);

  // Ssl::ConnectionInfo
  bool peerCertificateValidated() const override;

  // ConnectionInfoImplBase
  SSL* ssl() const override { return ssl_.get(); }

  // Ssl::Handshaker
  Network::PostIoAction doHandshake() override;

  Ssl::SocketState state() const { return state_; }
  void setState(Ssl::SocketState state) { state_ = state; }
  Ssl::HandshakeCallbacks* handshakeCallbacks() { return handshake_callbacks_; }

  bssl::UniquePtr<SSL> ssl_;

private:
  Ssl::HandshakeCallbacks* handshake_callbacks_;

  Ssl::SocketState state_;
  mutable SslExtendedSocketInfoImpl extended_socket_info_;
};

using SslHandshakerImplSharedPtr = std::shared_ptr<SslHandshakerImpl>;

class HandshakerFactoryContextImpl : public Ssl::HandshakerFactoryContext {
public:
  HandshakerFactoryContextImpl(Api::Api& api, const Server::Options& options,
                               absl::string_view alpn_protocols)
      : api_(api), options_(options), alpn_protocols_(alpn_protocols) {}

  // HandshakerFactoryContext
  Api::Api& api() override { return api_; }
  const Server::Options& options() const override { return options_; }
  absl::string_view alpnProtocols() const override { return alpn_protocols_; }

private:
  Api::Api& api_;
  const Server::Options& options_;
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
    return [](bssl::UniquePtr<SSL> ssl, int ssl_extended_socket_info_index,
              Ssl::HandshakeCallbacks* handshake_callbacks) {
      return std::make_shared<SslHandshakerImpl>(std::move(ssl), ssl_extended_socket_info_index,
                                                 handshake_callbacks);
    };
  }

  Ssl::HandshakerCapabilities capabilities() const override {
    // The default handshaker impl requires Envoy to handle all enumerated behaviors.
    return Ssl::HandshakerCapabilities{};
  }

  Ssl::SslCtxCb sslctxCb(Ssl::HandshakerFactoryContext&) const override {
    // The default handshaker impl doesn't additionally modify SSL_CTX.
    return nullptr;
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
