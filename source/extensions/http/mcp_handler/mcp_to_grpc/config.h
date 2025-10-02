#pragma once
#include "envoy/extensions/http/mcp_handler/mcp_to_grpc/v3/mcp_to_grpc.pb.validate.h"
#include "envoy/extensions/http/mcp_handler/mcp_to_grpc/v3/mcp_to_grpc.pb.h"
#include "source/extensions/http/mcp_handler/mcp_to_grpc/mcp_to_grpc.h"
#include "envoy/http/mcp_handler.h"
namespace Envoy {
namespace Extensions {
namespace Http {
namespace McpHandler {
namespace McpToGrpc {
class McpToGrpcMcpHandlerFactoryConfig : public Envoy::Http::McpHandlerFactoryConfig {
public:
  Envoy::Http::McpHandlerFactorySharedPtr
  createMcpHandlerFactory(const Protobuf::Message& config,
                          Server::Configuration::CommonFactoryContext& context) override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::http::mcp_handler::mcp_to_grpc::v3::McpToGrpc>();
  }
  std::string name() const override { return "envoy.http.mcp_handler.mcp_to_grpc"; }
};
} // namespace McpToGrpc
} // namespace McpHandler
} // namespace Http
} // namespace Extensions
} // namespace Envoy
