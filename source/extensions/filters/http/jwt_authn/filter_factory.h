#pragma once

#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"
#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.validate.h"
#include "envoy/server/filter_config.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

/**
 * Config registration for jwt_authn filter.
 */
class FilterFactory : public Common::UnifiedFactoryBase<
                          envoy::extensions::filters::http::jwt_authn::v3::JwtAuthentication,
                          envoy::extensions::filters::http::jwt_authn::v3::PerRouteConfig> {
public:
  FilterFactory() : UnifiedFactoryBase("envoy.filters.http.jwt_authn") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::jwt_authn::v3::JwtAuthentication& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::jwt_authn::v3::PerRouteConfig& per_route,
      Envoy::Server::Configuration::ServerFactoryContext&,
      Envoy::ProtobufMessage::ValidationVisitor&) override;
};

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
