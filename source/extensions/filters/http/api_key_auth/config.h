#pragma once

#include "envoy/extensions/filters/http/api_key_auth/v3/api_key_auth.pb.h"
#include "envoy/extensions/filters/http/api_key_auth/v3/api_key_auth.pb.validate.h"

#include "source/extensions/filters/http/api_key_auth/api_key_auth.h"
#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ApiKeyAuth {

class ApiKeyAuthFilterFactory
    : public Common::UnifiedFactoryBase<ApiKeyAuthProto, ApiKeyAuthPerRouteProto> {
public:
  ApiKeyAuthFilterFactory() : UnifiedFactoryBase("envoy.filters.http.api_key_auth") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const ApiKeyAuthProto& config, Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;

  // Shared factory creation used by the listener/cluster and route/vhost-level paths. Stats are
  // scoped to the given scope.
  static absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactory(const ApiKeyAuthProto& proto_config, const std::string& stats_prefix,
                      Stats::Scope& scope);

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteSpecificFilterConfigTyped(const ApiKeyAuthPerRouteProto& proto_config,
                                       Server::Configuration::ServerFactoryContext&,
                                       ProtobufMessage::ValidationVisitor&) override;
};

} // namespace ApiKeyAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
