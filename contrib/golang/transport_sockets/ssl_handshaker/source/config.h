#pragma once

#include "envoy/extensions/transport_sockets/ssl_handshaker/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/ssl_handshaker/v3/config.pb.validate.h"
#include "envoy/ssl/handshaker.h"

#include "extensions/transport_sockets/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CustomSslHandshaker {

/**
 * Config registration for the CustomSslHandshakeConfig extension.
 */
class CustomSslHandshakerConfigFactory : public Ssl::HandshakerFactory,
                                         Logger::Loggable<Logger::Id::config> {
public:
  CustomSslHandshakerConfigFactory() {}

  std::string name() const override { return "envoy.tls_handshaker"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::transport_sockets::ssl_handshaker::v3::Config>();
  }

  Ssl::HandshakerCapabilities capabilities() const override {
    return Ssl::HandshakerCapabilities{
      provides_certificates : true,
    };
  }

  Ssl::SslCtxCb sslctxCb(Ssl::HandshakerFactoryContext&) const override {
    // doesn't additionally modify SSL_CTX.
    return nullptr;
  }

  Ssl::HandshakerFactoryCb
  createHandshakerCb(const Protobuf::Message& message,
                     Ssl::HandshakerFactoryContext& handshaker_factory_context,
                     ProtobufMessage::ValidationVisitor& validation_visitor);

private:
  std::string name_;
};

} // namespace CustomSslHandshaker
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
