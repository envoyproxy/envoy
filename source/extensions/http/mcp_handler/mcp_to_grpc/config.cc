#include "source/extensions/http/mcp_handler/mcp_to_grpc/config.h"
#include "source/common/config/utility.h"
namespace Envoy {
namespace Extensions {
namespace Http {
namespace McpHandler {
namespace McpToGrpc {
Envoy::Http::McpHandlerFactorySharedPtr
McpToGrpcMcpHandlerFactoryConfig::createMcpHandlerFactory(
    const Protobuf::Message& config, Server::Configuration::CommonFactoryContext& context) {
  const auto& proto_config = MessageUtil::downcastAndValidate<const envoy::extensions::http::mcp_handler::mcp_to_grpc::v3::McpToGrpc&>(
      config, context.messageValidationVisitor());
  return std::make_shared<McpToGrpcMcpHandlerFactory>(
      proto_config, context);
}
REGISTER_FACTORY(McpToGrpcMcpHandlerFactoryConfig, Envoy::Http::McpHandlerFactoryConfig);
} // namespace McpToGrpc
} // namespace McpHandler
} // namespace Http
} // namespace Extensions
} // namespace Envoy
