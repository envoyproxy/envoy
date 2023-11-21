#pragma once

#include "envoy/extensions/filters/http/header_mutation/v3/header_mutation.pb.h"
#include "envoy/extensions/filters/http/header_mutation/v3/header_mutation.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/header_mutation/header_mutation.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderMutation {

/**
 * Config registration for the stateful session filter. @see NamedHttpFilterConfigFactory.
 */
class HeaderMutationFactoryConfig
    : public Common::DualFactoryBase<ProtoConfig, PerRouteProtoConfig> {
public:
  HeaderMutationFactoryConfig() : DualFactoryBase("envoy.filters.http.header_mutation") {}

private:
  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProtoTyped(const ProtoConfig& proto_config,
                                    const std::string& stats_prefix, DualInfo info,
                                    Server::Configuration::ServerFactoryContext& context) override;
  Router::RouteSpecificFilterConfigConstSharedPtr
  createRouteSpecificFilterConfigTyped(const PerRouteProtoConfig& proto_config,
                                       Server::Configuration::ServerFactoryContext&,
                                       ProtobufMessage::ValidationVisitor&) override;
};

} // namespace HeaderMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
