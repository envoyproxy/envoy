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
    : public Common::ExceptionFreeFactoryBase<ApiKeyAuthProto, ApiKeyAuthPerRouteProto> {
public:
  ApiKeyAuthFilterFactory() : ExceptionFreeFactoryBase("envoy.filters.http.api_key_auth") {}

private:
  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProtoTyped(const ApiKeyAuthProto& config, const std::string& stats_prefix,
                                    Server::Configuration::FactoryContext& context) override;
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const ApiKeyAuthProto& config, const std::string& stats_prefix,
      Server::Configuration::ServerFactoryContext& context) override;

  // Shared factory creation used by both the downstream (FactoryContext) and route/vhost-level
  // (ServerFactoryContext) paths. Stats are scoped to the given scope.
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
