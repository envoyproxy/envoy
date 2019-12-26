#pragma once

#include "envoy/config/filter/http/ext_authz/v2/ext_authz.pb.h"
#include "envoy/config/filter/http/ext_authz/v2/ext_authz.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

/**
 * Config registration for the external authorization filter. @see NamedHttpFilterConfigFactory.
 */
class ExtAuthzFilterConfig
    : public Common::FactoryBase<envoy::config::filter::http::ext_authz::v2::ExtAuthz,
                                 envoy::config::filter::http::ext_authz::v2::ExtAuthzPerRoute> {
public:
  ExtAuthzFilterConfig() : FactoryBase(HttpFilterNames::get().ExtAuthorization) {}

private:
  static constexpr uint64_t DefaultTimeout = 200;
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::http::ext_authz::v2::ExtAuthz& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::config::filter::http::ext_authz::v2::ExtAuthzPerRoute& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
