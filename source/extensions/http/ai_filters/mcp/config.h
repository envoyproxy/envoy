#pragma once

#include <optional>

#include "envoy/extensions/filters/http/mcp/v3/mcp.pb.h"
#include "envoy/extensions/filters/http/mcp/v3/mcp.pb.validate.h"

#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace Mcp {

// Takes the configuration of the MCP HTTP filter, per route included.
class McpFilterConfigFactory : public HttpFilters::AiProtocolManager::AiFilterConfigFactory {
public:
  absl::StatusOr<HttpFilters::AiProtocolManager::AiFilterFactoryCb>
  createAiFilterFactory(const Protobuf::Message& config,
                        Server::Configuration::ServerFactoryContext& context,
                        Stats::Scope& scope) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::filters::http::mcp::v3::Mcp>();
  }

  ProtobufTypes::MessagePtr createEmptyRouteConfigProto() override {
    return std::make_unique<envoy::extensions::filters::http::mcp::v3::McpOverride>();
  }

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteConfig(const Protobuf::Message& config,
                    Server::Configuration::ServerFactoryContext& context,
                    ProtobufMessage::ValidationVisitor& validator) override;

  // The inspector reads attributes out of the parsed document, so it needs every string a request
  // within the body size limit can hold.
  std::optional<uint32_t> requestInlineStringBytes(const Protobuf::Message& config) override;

  std::string name() const override { return "envoy.http.ai_filters.mcp"; }
};

} // namespace Mcp
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
