#include "contrib/mcp_sse_stateful_session/filters/http/source/config.h"

#include "source/common/protobuf/utility.h"

#include "contrib/envoy/extensions/http/stateful_session/mcp_sse/v3/mcp_sse.pb.h"
#include "contrib/envoy/extensions/http/stateful_session/mcp_sse/v3/mcp_sse.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace StatefulSession {
namespace McpSse {

McpSseSessionStateFactorySharedPtr
Config::createSessionStateFactory(const Protobuf::Message& config,
                                  Server::Configuration::GenericFactoryContext& context) {
  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::http::stateful_session::mcp_sse::v3::McpSseSessionState&>(
      config, context.messageValidationVisitor());
  return std::make_shared<McpSseSessionStateFactoryImpl>(proto_config);
}

} // namespace McpSse
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
