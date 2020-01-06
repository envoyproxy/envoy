#pragma once

#include "envoy/extensions/filters/http/ext_authz/v3alpha/ext_authz.pb.h"
#include "envoy/extensions/filters/http/ext_authz/v3alpha/ext_authz.pb.validate.h"

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
    : public Common::FactoryBase<
          envoy::extensions::filters::http::ext_authz::v3alpha::ExtAuthz,
          envoy::extensions::filters::http::ext_authz::v3alpha::ExtAuthzPerRoute> {
public:
  ExtAuthzFilterConfig() : FactoryBase(HttpFilterNames::get().ExtAuthorization) {}

private:
  static constexpr uint64_t DefaultTimeout = 200;
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::ext_authz::v3alpha::ExtAuthz& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::ext_authz::v3alpha::ExtAuthzPerRoute& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
