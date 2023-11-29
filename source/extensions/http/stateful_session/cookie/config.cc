#include "source/extensions/http/stateful_session/cookie/config.h"

#include "source/common/config/utility.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace StatefulSession {
namespace Cookie {

Envoy::Http::SessionStateFactorySharedPtr
CookieBasedSessionStateFactoryConfig::createSessionStateFactory(
    const Protobuf::Message& config, Server::Configuration::GenericFactoryContext& context) {

  const auto& proto_config = MessageUtil::downcastAndValidate<const CookieBasedSessionStateProto&>(
      config, context.messageValidationVisitor());
  return std::make_shared<CookieBasedSessionStateFactory>(
      proto_config, context.serverFactoryContext().mainThreadDispatcher().timeSource());
}

REGISTER_FACTORY(CookieBasedSessionStateFactoryConfig, Envoy::Http::SessionStateFactoryConfig);

} // namespace Cookie
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
