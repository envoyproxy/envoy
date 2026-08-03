#pragma once

#include "envoy/extensions/filters/http/stateful_session/v3/stateful_session.pb.h"
#include "envoy/extensions/filters/http/stateful_session/v3/stateful_session.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/stateful_session/stateful_session.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace StatefulSession {

/**
 * Config registration for the stateful session filter. @see NamedHttpFilterConfigFactory.
 */
class StatefulSessionFactoryConfig
    : public Common::ExceptionFreeFactoryBase<ProtoConfig, PerRouteProtoConfig> {
public:
  StatefulSessionFactoryConfig()
      : ExceptionFreeFactoryBase("envoy.filters.http.stateful_session") {}

private:
  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProtoTyped(const ProtoConfig& proto_config,
                                    const std::string& stats_prefix,
                                    Server::Configuration::FactoryContext& context) override;

  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const ProtoConfig& proto_config, const std::string& stats_prefix,
      Server::Configuration::ServerFactoryContext& context) override;

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteSpecificFilterConfigTyped(const PerRouteProtoConfig& proto_config,
                                       Server::Configuration::ServerFactoryContext&,
                                       ProtobufMessage::ValidationVisitor&) override;
};

} // namespace StatefulSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
