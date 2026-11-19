#pragma once

#include <optional>

#include "envoy/extensions/filters/http/mcp_json_rest_bridge/v3/mcp_json_rest_bridge.pb.h"
#include "envoy/extensions/filters/http/mcp_json_rest_bridge/v3/mcp_json_rest_bridge.pb.validate.h"

#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace McpJsonRestBridge {

// Takes the configuration of the MCP JSON REST bridge HTTP filter, per route included.
class McpJsonRestBridgeFilterConfigFactory
    : public HttpFilters::AiProtocolManager::AiFilterConfigFactory {
public:
  absl::StatusOr<HttpFilters::AiProtocolManager::AiFilterFactoryCb>
  createAiFilterFactory(const Protobuf::Message& config,
                        Server::Configuration::ServerFactoryContext& context,
                        Stats::Scope& scope) override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge>();
  }

  ProtobufTypes::MessagePtr createEmptyRouteConfigProto() override {
    return std::make_unique<
        envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridgePerRoute>();
  }

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteConfig(const Protobuf::Message& config,
                    Server::Configuration::ServerFactoryContext& context,
                    ProtobufMessage::ValidationVisitor& validator) override;

  // The bridge maps argument values into the backend request's path and headers, so it needs
  // every string a request within its body limit can hold.
  std::optional<uint32_t> requestInlineStringBytes(const Protobuf::Message& config) override;

  std::string name() const override { return "envoy.http.ai_filters.mcp_json_rest_bridge"; }
};

} // namespace McpJsonRestBridge
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
