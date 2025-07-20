#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/server/factory_context.h"

#include "contrib/mcp_sse_stateful_session/filters/http/source/mcp_sse.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace StatefulSession {
namespace McpSse {

class Config : public Envoy::Config::TypedFactory {
public:
  McpSseSessionStateFactorySharedPtr
  createSessionStateFactory(const Protobuf::Message& config,
                            Server::Configuration::GenericFactoryContext& context);

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::http::stateful_session::mcp_sse::v3::McpSseSessionState>();
  }

  std::string name() const override { return "envoy.http.stateful_session.mcp_sse"; }
  std::string category() const override { return "envoy.http.stateful_session"; }
};

} // namespace McpSse
} // namespace StatefulSession
} // namespace Http
} // namespace Extensions
} // namespace Envoy
