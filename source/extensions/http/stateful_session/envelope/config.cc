#include "source/extensions/http/stateful_session/envelope/config.h"

#include "envoy/extensions/http/stateful_session/envelope/v3/envelope.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace StatefulSession {
namespace Envelope {

Envoy::Http::SessionStateFactorySharedPtr
EnvelopeSessionStateFactoryConfig::createSessionStateFactory(
    const Protobuf::Message& config, Server::Configuration::GenericFactoryContext& context) {

  const auto& proto_config = MessageUtil::downcastAndValidate<const EnvelopeSessionStateProto&>(
      config, context.messageValidationVisitor());
  return std::make_shared<EnvelopeSessionStateFactory>(proto_config);
}

REGISTER_FACTORY(EnvelopeSessionStateFactoryConfig, Envoy::Http::SessionStateFactoryConfig);

} // namespace Envelope
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
