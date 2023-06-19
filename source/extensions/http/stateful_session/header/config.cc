#include "source/extensions/http/stateful_session/header/config.h"

#include "source/common/config/utility.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace StatefulSession {
namespace Header {

Envoy::Http::SessionStateFactorySharedPtr
HeaderBasedSessionStateFactoryConfig::createSessionStateFactory(
    const Protobuf::Message& config, Server::Configuration::CommonFactoryContext& context) {

  const auto& proto_config = MessageUtil::downcastAndValidate<const HeaderBasedSessionStateProto&>(
      config, context.messageValidationVisitor());
  return std::make_shared<HeaderBasedSessionStateFactory>(proto_config);
}

REGISTER_FACTORY(HeaderBasedSessionStateFactoryConfig, Envoy::Http::SessionStateFactoryConfig);

} // namespace Header
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
