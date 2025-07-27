#include "contrib/mcp_sse_stateful_session/http/source/config.h"

#include "source/common/protobuf/utility.h"

#include "contrib/envoy/extensions/http/mcp_sse_stateful_session/envelope/v3alpha/envelope.pb.h"
#include "contrib/envoy/extensions/http/mcp_sse_stateful_session/envelope/v3alpha/envelope.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace McpSseSessionState {
namespace Envelope {

McpSseSessionStateFactorySharedPtr
EnvelopeSessionStateFactoryConfig::createSessionStateFactory(const Protobuf::Message& config,
                                  Server::Configuration::GenericFactoryContext& context) {
  const auto& proto_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::http::mcp_sse_stateful_session::envelope::v3alpha::EnvelopeSessionState&>(
      config, context.messageValidationVisitor());
  return std::make_shared<EnvelopeSessionStateFactory>(proto_config);
}

REGISTER_FACTORY(EnvelopeSessionStateFactoryConfig, McpSseSessionStateFactoryConfig);

} // namespace Envelope
} // namespace McpSseSessionState
} // namespace Http
} // namespace Extensions
} // namespace Envoy
