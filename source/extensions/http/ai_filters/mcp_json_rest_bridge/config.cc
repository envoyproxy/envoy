#include "source/extensions/http/ai_filters/mcp_json_rest_bridge/config.h"

#include <limits>
#include <optional>

#include "envoy/registry/registry.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/mcp_json_rest_bridge/mcp_json_rest_bridge_filter.h"
#include "source/extensions/http/ai_filters/mcp_json_rest_bridge/filter.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace McpJsonRestBridge {

namespace {

using BridgeProto = envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge;
using BridgePerRouteProto =
    envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridgePerRoute;

} // namespace

absl::StatusOr<HttpFilters::AiProtocolManager::AiFilterFactoryCb>
McpJsonRestBridgeFilterConfigFactory::createAiFilterFactory(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context,
    Stats::Scope&) {
  const auto& proto = MessageUtil::downcastAndValidate<const BridgeProto&>(
      config, context.messageValidationVisitor());
  absl::StatusOr<Bridge::McpJsonRestBridgeFilterConfigSharedPtr> bridge_config =
      Bridge::McpJsonRestBridgeFilterConfig::create(proto);
  RETURN_IF_NOT_OK_REF(bridge_config.status());
  return [bridge_config = std::move(bridge_config.value())](
             const HttpFilters::AiProtocolManager::AiFilterContext& stream_context)
             -> HttpFilters::AiProtocolManager::AiFilterSharedPtr {
    return std::make_shared<McpJsonRestBridgeFilter>(bridge_config, stream_context);
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
McpJsonRestBridgeFilterConfigFactory::createRouteConfig(
    const Protobuf::Message& config, Server::Configuration::ServerFactoryContext&,
    ProtobufMessage::ValidationVisitor& validator) {
  const auto& proto =
      MessageUtil::downcastAndValidate<const BridgePerRouteProto&>(config, validator);
  absl::StatusOr<Bridge::McpJsonRestBridgePerRouteConfigSharedPtr> route_config =
      Bridge::McpJsonRestBridgePerRouteConfig::create(proto);
  RETURN_IF_NOT_OK_REF(route_config.status());
  return std::move(route_config.value());
}

std::optional<uint32_t>
McpJsonRestBridgeFilterConfigFactory::requestInlineStringBytes(const Protobuf::Message& config) {
  const uint32_t max_request_body_size = Bridge::McpJsonRestBridgeFilterConfig::maxRequestBodySize(
      dynamic_cast<const BridgeProto&>(config));
  return max_request_body_size > 0 ? max_request_body_size : std::numeric_limits<uint32_t>::max();
}

REGISTER_FACTORY(McpJsonRestBridgeFilterConfigFactory,
                 HttpFilters::AiProtocolManager::AiFilterConfigFactory);

} // namespace McpJsonRestBridge
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
