#include "extensions/transport_sockets/tls/ssl_handshaker/config.h"

#include "envoy/registry/registry.h"
#include "common/config/utility.h"

#include "extensions/transport_sockets/tls/ssl_handshaker/custom_ssl_handshaker.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
namespace CustomSslHandshaker {

Ssl::HandshakerFactoryCb CustomSslHandshakerConfigFactory::createHandshakerCb(
    const Protobuf::Message& any_config, Ssl::HandshakerFactoryContext& handshaker_factory_context,
    ProtobufMessage::ValidationVisitor& validation_visitor) {

  // not use downcastAndValidate, since it will cause std::bad_cast exception.
  auto message = createEmptyConfigProto();
  Envoy::Config::Utility::translateOpaqueConfig(static_cast<const Protobuf::Any&>(any_config), {},
                                                validation_visitor, *message);
  const auto& proto_config =
      dynamic_cast<const envoy::extensions::transport_sockets::ssl_handshaker::v3::Config&>(
          *message);

  // NOTICE: need to patch Envoy core to support passing factory contenxt.
  auto& extesnion_handshaker_factory_context =
      dynamic_cast<Envoy::Extensions::TransportSockets::Tls::HandshakerFactoryContextImpl&>(
          handshaker_factory_context);
  auto& factory_context = extesnion_handshaker_factory_context.factoryContext();

  CustomSslHandshakerConfigSharedPtr config =
      std::make_shared<CustomSslHandshakerConfig>(proto_config, factory_context);
  config->init();

  return [config](bssl::UniquePtr<SSL> ssl, int ssl_extended_socket_info_index,
                  Ssl::HandshakeCallbacks* handshake_callbacks) {
    return std::make_shared<CustomSslHandshakerImpl>(std::move(ssl), ssl_extended_socket_info_index,
                                                     handshake_callbacks, config);
  };
}

/**
 * Static registration for the custom Tls handshaker extension. @see RegisterFactory.
 */
REGISTER_FACTORY(CustomSslHandshakerConfigFactory, Ssl::HandshakerFactory);

} // namespace CustomSslHandshaker
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
