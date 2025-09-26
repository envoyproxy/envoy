#pragma once

#include "source/extensions/filters/http/common/factory_base.h"

#include "contrib/envoy/extensions/filters/http/mcp_sse_stateful_session/v3alpha/mcp_sse_stateful_session.pb.h"
#include "contrib/mcp_sse_stateful_session/filters/http/source/mcp_sse_stateful_session.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpSseStatefulSession {

/**
 * Config registration for the stateful session filter. @see NamedHttpFilterConfigFactory.
 */
class McpSseStatefulSessionFactoryConfig
    : public Common::FactoryBase<ProtoConfig, PerRouteProtoConfig> {
public:
  McpSseStatefulSessionFactoryConfig()
      : FactoryBase("envoy.filters.http.mcp_sse_stateful_session") {}

private:
  Envoy::Http::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const ProtoConfig& proto_config,
                                    const std::string& stats_prefix,
                                    Server::Configuration::FactoryContext& context) override;
  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteSpecificFilterConfigTyped(const PerRouteProtoConfig& proto_config,
                                       Server::Configuration::ServerFactoryContext&,
                                       ProtobufMessage::ValidationVisitor&) override;
};

} // namespace McpSseStatefulSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
