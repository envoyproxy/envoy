#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/server/factory_context.h"
#include "contrib/mcp_sse_stateful_session/http/source/mcp_sse_stateful_session.h"
#include "contrib/mcp_sse_stateful_session/http/source/envelope.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace McpSseSessionState {
namespace Envelope {

class EnvelopeSessionStateFactoryConfig : public McpSseSessionStateFactoryConfig {
public:
  McpSseSessionStateFactorySharedPtr
  createSessionStateFactory(const Protobuf::Message& config,
                            Server::Configuration::GenericFactoryContext& context) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::http::mcp_sse_stateful_session::envelope::v3alpha::
                                EnvelopeSessionState>();
  }

  std::string name() const override { return "envoy.http.mcp_sse_stateful_session.envelope"; }
};

} // namespace Envelope
} // namespace McpSseSessionState
} // namespace Http
} // namespace Extensions
} // namespace Envoy
